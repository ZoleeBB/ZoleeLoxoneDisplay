'use strict';
const $ = id => document.getElementById(id);
const types = {
  value:'Mért érték', pulse:'Nyomógomb', switch:'Kapcsoló', lighting:'Világításvezérlés',
  irc:'Termosztát', shading:'Árnyékolás', dimmer:'Dimmer', 'central-lighting':'Központi világítás',
  slider:'Analóg értékállító', 'timed-switch':'Időzített kapcsoló', 'window-monitor':'Ajtó–ablak összesítő',
  gate:'Kapu / garázsajtó', window:'Motoros ablak', audio:'Audioserver'
};
function eligible(type,row){
  if(!row.control_uuid || row.secured || (row.action_uuid==='' && type!=='window-monitor')) return false;
  if(type==='pulse') return ['Switch','Pushbutton','TimedSwitch'].includes(row.type);
  if(type==='switch') return row.type==='Switch';
  if(['lighting','irc','shading'].includes(type)) return row[type+'_blocked']===false;
  return row.controls_type===type && row.controls_blocked===false;
}
const energyFields = {
  PowerGrid:'Hálózati teljesítmény',PowerBat:'Akkumulátorteljesítmény',PowerPV:'Napelem teljesítménye',
  PowerFull:'Ház fogyasztása',SocBat:'Akkumulátor töltöttsége',Status:'Rendszerállapot',
  BatteryTemperature:'Akkumulátor-hőmérséklet',Temperature:'Külső hőmérséklet',WindSpeed:'Szélsebesség',
  HasSunRadiation:'Napsütés',HasPrecipitation:'Csapadék',SunAzimuth:'Nap azimutja',SunAltitude:'Nap magassága',
  Sunrise:'Napkelte',Sunset:'Napnyugta',RelativeHumidity:'Páratartalom'
};
const app = {profile:null,assets:{backgrounds:[],icons:[]},controls:[],section:'tabs',tab:0,
  dirty:false,busy:false,connected:false,connectionStatus:'',secrets:{}};
const editor=$('editor'), save=$('save');
function el(tag,cls,content){
  const node=document.createElement(tag);
  if(cls) node.className=cls;
  if(content!==undefined) node.textContent=content;
  return node;
}
function button(caption,action,cls){
  const b=el('button',cls,caption); b.type='button'; b.onclick=action; return b;
}
function say(msg,error=false){
  $('status').textContent=msg; $('status').toggleAttribute('data-error',error);
}
function changed(){
  app.dirty=true; save.disabled=app.busy;
  say('Nem mentett módosítások.');
}
function refresh(){changed(); render();}
function field(label,control){
  const wrap=el('label','field'); wrap.append(el('span','caption',label),control); return wrap;
}
function textBox(o,key,type='text',attrs={},after){
  const i=el('input'); i.type=type; i.value=o[key]??'';
  for(const [k,v] of Object.entries(attrs)) i.setAttribute(k,v);
  i.oninput=()=>{o[key]=type==='number'?(i.value===''?0:Number(i.value)):i.value; changed(); if(after) after(o[key]);};
  return i;
}
function select(o,key,options,update){
  const s=el('select');
  for(const [value,label] of Object.entries(options)){const opt=el('option','',label); opt.value=value; s.append(opt);}
  s.value=o[key];
  s.onchange=()=>{o[key]=s.value; changed(); if(update) update(s.value);}; return s;
}
function check(label,o,key,update){
  const i=el('input'); i.type='checkbox'; i.checked=o[key];
  i.onchange=()=>{o[key]=i.checked; changed(); if(update) update(i.checked);};
  const l=el('label','check'); l.append(i,el('span','',label)); return l;
}
function panel(title,actions){
  const p=el('section','panel'),head=el('div','sectionhead');
  head.append(el('h2','',title)); if(actions) head.append(actions);
  p.append(head); editor.append(p); return p;
}
function grid(parent){const g=el('div','grid'); parent.append(g); return g;}
function input(parent,label,o,key,type,attrs,after){
  const i=textBox(o,key,type,attrs,after); parent.append(field(label,i)); return i;
}
function secret(g,label,key){
  return input(g,label,app.secrets,key,'password',{autocomplete:'new-password',maxlength:127,placeholder:'Üresen a mentett jelszó marad'});
}
function rowLabel(r,withState=true){
  return [r.room,r.name,withState?r.state:''].filter(Boolean).join(' · ');
}
let lists=new Map();
function mappingInput(o,key,rows,idKey,onSelected){
  const i=el('input');
  /* All energy mappings and sensor rows on the same page share one state list. */
  const listKey=idKey==='uuid'?'states':rows.map(r=>r.control_uuid).join(',');
  if(!lists.has(listKey)){
    const list=el('datalist');list.id='uuid-'+lists.size;
    const seen=new Set();
    for(const r of rows){
      const value=r[idKey]; if(!value || seen.has(value)) continue;
      const opt=el('option'); opt.value=value; opt.label=rowLabel(r,idKey==='uuid'); list.append(opt); seen.add(value);
    }
    editor.append(list);lists.set(listKey,list.id);
  }
  i.setAttribute('list',lists.get(listKey));i.placeholder='Válassz a listából vagy írj be UUID-t';
  i.spellcheck=false;i.autocomplete='off';i.value=o[key]||'';
  i.oninput=()=>{
    const value=i.value.trim().toLowerCase();o[key]=value;changed();
    if(onSelected)onSelected(rows.find(r=>r[idKey]===value));
  };
  return i;
}
function assetURL(kind,id){return '/assets/'+(kind==='icons'?'icons/':'thumbs/')+encodeURIComponent(id)+(kind==='icons'?'.png':'.jpg');}
function image(kind,id){
  const img=el('img'); img.alt=''; img.loading='lazy'; img.src=assetURL(kind,id); return img;
}
function assetButton(kind,id,onSelect){
  const asset=app.assets[kind].find(item=>item.id===id);
  const b=button(asset?.name||id||'Választás',()=>openPicker(kind,id,onSelect),'pickbtn');
  if(asset) b.prepend(image(kind,id)); return b;
}
function openPicker(kind,selected,onSelect){
  const dialog=$('picker'); $('pickTitle').textContent=kind==='icons'?'Fülikon kiválasztása':'Háttérkép kiválasztása';
  const filter=$('filter'); filter.value='';
  function show(){
    const box=$('choices'); box.replaceChildren();
    for(const a of app.assets[kind].filter(a=>(a.name+' '+a.id).toLocaleLowerCase('hu').includes(filter.value.toLocaleLowerCase('hu')))){
      const b=button('',()=>{dialog.close(); onSelect(a.id); refresh();},'preview '+(kind==='icons'?'icon':''));
      b.setAttribute('aria-pressed',a.id===selected); b.append(image(kind,a.id),el('span','',a.name)); box.append(b);
    }
    if(!box.childElementCount){
      const msg=el('p','hint span',app.assets[kind].length?'Nincs találat.':'Az induló képcsomagot a Frissítés oldalon telepítheted.');
      if(!app.assets[kind].length){const link=el('a','','Frissítés megnyitása');link.href='/update';msg.append(' ',link);}
      box.append(msg);
    }
  }
  filter.oninput=show;show();dialog.showModal();filter.focus();
}
$('pickClose').onclick=()=>$('picker').close();
function moveItem(items,at,to){if(to<0||to>=items.length)return;const [v]=items.splice(at,1);items.splice(to,0,v);}
function itemTools(items,at,removeMessage,moved,removed){
  const wrap=el('div','tools');
  const up=button('↑',()=>{moveItem(items,at,at-1); if(moved)moved(at-1); refresh();});
  const down=button('↓',()=>{moveItem(items,at,at+1); if(moved)moved(at+1); refresh();});
  up.setAttribute('aria-label','Mozgatás előre'); down.setAttribute('aria-label','Mozgatás hátra');
  up.disabled=at===0;down.disabled=at===items.length-1;
  wrap.append(up,down,button('Törlés',()=>{if(confirm(removeMessage)){items.splice(at,1);if(removed)removed();refresh();}},'danger'));
  return wrap;
}
function nextId(){return 'b-'+Array.from(crypto.getRandomValues(new Uint8Array(8)),x=>x.toString(16).padStart(2,'0')).join('');}
function renderBlock(parent,tab,b,idx){
  const box=el('section','block'),head=el('div','row');
  head.append(el('h3','',b.name||types[b.type]),itemTools(tab.blocks,idx,'Törlöd ezt az elemet?')); box.append(head);
  const g=grid(box);
  g.append(field('Funkció',select(b,'type',types,()=>{b.uuid='';b.stateUuid='';delete b.allowPermanent;delete b.decimals;delete b.scale;delete b.offset;
    if(b.type==='timed-switch') b.allowPermanent=true;render();})));
  const name=input(g,'Elem neve',b,'name','text',{maxlength:80});
  const controls=app.controls.filter(r=>eligible(b.type,r));
  if(b.type!=='value'){
    g.append(field('Vezérlőblokk',mappingInput(b,'uuid',controls,'control_uuid',r=>{
      if(!r) return;
      if(!b.name || b.name==='Új elem'){b.name=r.name; name.value=b.name; head.firstChild.textContent=b.name;}
      const state=app.controls.find(x=>x.control_uuid===r.control_uuid && x.state==='active');
      if(state && b.type==='switch'){b.stateUuid=state.uuid; const fb=box.querySelector('[data-feedback] input');if(fb)fb.value=state.uuid;}
    })));
  }
  if(['switch','pulse','value'].includes(b.type)){
    const state=field(b.type==='value'?'Megjelenített állapot':'Visszajelző állapot',mappingInput(b,'stateUuid',app.controls,'uuid',r=>{
      if(r && (!b.name||b.name==='Új elem')) {b.name=r.name;name.value=b.name;head.firstChild.textContent=b.name;}
    })); state.dataset.feedback='';g.append(state);
  }
  if(b.type==='value') {
    b.decimals??=1;b.scale??=1;b.offset??=0;
    input(g,'Mértékegység',b,'unit','text',{maxlength:16});
    input(g,'Tizedesjegyek',b,'decimals','number',{min:0,max:3});
    input(g,'Szorzó',b,'scale','number',{step:'any',min:-1e6,max:1e6});
    input(g,'Eltolás',b,'offset','number',{step:'any',min:-1e6,max:1e6});
  } else if(b.type==='timed-switch'){
    g.append(check('Tartós bekapcsolás engedélyezése',b,'allowPermanent'));
  }
  parent.append(box);
}
function renderEnergy(p,tab){
  const s=el('section','block');s.append(el('h3','','Értékek összerendelése'),
    el('p','hint','A hálózat, akkumulátor, napelem és házfogyasztás értékeit kW-ra skálázd. A szorzó és eltolás a Miniserver nyers értékére vonatkozik.'));
  for(const [k,label] of Object.entries(energyFields)){
    const row=el('div','maprow'), m=tab.energy[k]||{uuid:'',scale:1,offset:0};
    const sync=()=>{if(m.uuid) tab.energy[k]=m;else delete tab.energy[k];};
    row.append(field(label,mappingInput(m,'uuid',app.controls,'uuid',sync)));
    input(row,'Szorzó',m,'scale','number',{step:'any',min:-1e6,max:1e6},sync);
    input(row,'Eltolás',m,'offset','number',{step:'any',min:-1e6,max:1e6},sync);s.append(row);
  }
  p.append(s);
}
function renderTabs(){
  const tabs=app.profile.tabs;
  const add=button('+ Lapfül',()=>{
    tabs.push({kind:'controls',name:'Új lapfül',icon:'home',background:{type:'color',value:'#506559'},blocks:[]});
    app.tab=tabs.length-1; refresh();
  }); add.disabled=tabs.length>=8;
  const group=panel('Lapfülek',add);
  if(!tabs.length){group.append(el('p','empty','Adj hozzá egy lapfület, majd válassz hozzá ikont, hátteret és Loxone-elemeket.'));return;}
  const strip=el('div','tabstrip'); strip.style.marginTop='16px';
  tabs.forEach((t,idx)=>{const b=button(t.name||'Új lapfül',()=>{app.tab=idx;render();}); if(idx===app.tab)b.setAttribute('aria-current','page');strip.append(b);});group.append(strip);
  const tab=tabs[app.tab],p=panel(tab.name||'Új lapfül',itemTools(tabs,app.tab,'Törlöd a lapfület és a rajta lévő elemeket?',idx=>{app.tab=idx;},()=>{app.tab=Math.max(0,Math.min(app.tab,tabs.length-1));}));
  const g=grid(p);input(g,'Lap neve',tab,'name','text',{maxlength:80},v=>{
    strip.children[app.tab].textContent=v;p.querySelector('h2').textContent=v;
  });
  const kind=el('select');
  for(const [v,label] of Object.entries({controls:'Okosotthon',energy:'Energiaáramlás'})){
    const opt=el('option','',label);opt.value=v;kind.append(opt);
  }
  kind.value=tab.kind;
  kind.onchange=()=>{
    const previous=tab.kind;
    if((tab.blocks?.length||Object.keys(tab.energy||{}).length) && !confirm('A típusváltás törli ezen a lapon az elemeket vagy a hozzárendeléseket. Folytatod?')){kind.value=previous;return;}
    tab.kind=kind.value;
    if(tab.kind==='energy'){delete tab.blocks;delete tab.background;tab.energy={};tab.icon='energy';}
    else{delete tab.energy;tab.blocks=[];tab.background={type:'color',value:'#506559'};tab.icon='home';}
    refresh();
  };g.append(field('Lap típusa',kind),field('Felső sáv ikonja',assetButton('icons',tab.icon,id=>{tab.icon=id;})));
  if(tab.kind==='energy'){renderEnergy(p,tab);return;}
  const bg=tab.background;
  g.append(field('Háttér típusa',select(bg,'type',{color:'Egyszínű',image:'Háttérkép'},value=>{
    bg.value=value==='color'?'#506559':(app.assets.backgrounds[0]?.id||'');render();
  })));
  if(bg.type==='color')input(g,'Háttérszín',bg,'value','color');
  else g.append(field('Háttérkép',assetButton('backgrounds',bg.value,id=>{bg.value=id;})));
  const addBlock=button('+ Elem',()=>{tab.blocks.push({id:nextId(),type:'switch',name:'Új elem',uuid:'',stateUuid:'',unit:''});refresh();});
  addBlock.disabled=tab.blocks.length>=12;
  const blocks=panel('Oldalelemek',addBlock);
  if(!tab.blocks.length)blocks.append(el('p','empty','Válassz egy elemet az egyfunkciós nézethez, vagy adj hozzá többet a listanézethez.'));
  tab.blocks.forEach((b,i)=>renderBlock(blocks,tab,b,i));
}
function renderLoxone(){
  const p=panel('Miniserver-kapcsolat',button('Adatlista frissítése',refreshControls));
  const hint=el('p','hint',app.connectionStatus || (app.connected?'Kapcsolódva.':'Mentsd a kapcsolat adatait a Loxone-objektumok betöltéséhez.'));p.append(hint);
  const lx=app.profile.loxone,g=grid(p);g.style.marginTop='16px';
  input(g,'Miniserver címe',lx,'host','text',{placeholder:'192.168.1.77',maxlength:127});
  input(g,'Port',lx,'port','number',{min:1,max:65535});
  input(g,'Loxone-felhasználónév',lx,'user','text',{maxlength:95,autocomplete:'off'});
  secret(g,'Loxone-jelszó','loxonePassword');
  g.append(check('Titkosított kapcsolat (TLS)',lx,'tls',checked=>{
    if(checked && lx.port===80) lx.port=443; else if(!checked && lx.port===443)lx.port=80;render();
  }));
  if(lx.tls){
    const ta=el('textarea');ta.rows=5;ta.maxLength=4095;ta.value=lx.ca_pem;ta.oninput=()=>{lx.ca_pem=ta.value;changed();};
    const f=field('Saját hitelesítésszolgáltató tanúsítványa (PEM)',ta);f.classList.add('span');g.append(f);
  }
  const web=panel('Webes hozzáférés'),w=grid(web);
  input(w,'Adminisztrátori felhasználónév',app.profile,'adminUser','text',{maxlength:63});
  secret(w,'Adminisztrátori jelszó','adminPassword');
  web.append(el('p','hint','A fiók a beállításokat és a frissítési oldalt is védi. Üres felhasználónévvel az adminfelület a helyi hálózaton szabadon elérhető.'));
}
function renderDisplay(){
  const d=app.profile.display,p=panel('Fényerő és képernyőkímélő'),g=grid(p);
  input(g,'Nappali fényerő (%)',d,'dayBrightness','number',{min:1,max:100});
  input(g,'Éjszakai fényerő (%)',d,'nightBrightness','number',{min:1,max:100});
  input(g,'Képernyőkímélő késleltetése (mp)',d,'saverSeconds','number',{min:0,max:86400,placeholder:'0'});
  p.append(el('p','hint','A 0 kikapcsolja az automatikus képernyőkímélőt. Érintésre visszatér az utolsó lap.'));
  const night=grid(panel('Éjszakai mód'));
  night.append(field('Vezérlés',select(d,'nightMode',{timer:'Időzítés',value:'Loxone-érték'},render)));
  if(d.nightMode==='timer'){
    input(night,'Éjszaka kezdete',d,'nightStart','time'); input(night,'Éjszaka vége',d,'nightEnd','time');
  } else night.append(field('Éjszakai mód állapota',mappingInput(d,'nightUuid',app.controls,'uuid')));
}
function renderIntercom(){
  const o=app.profile.intercom,p=panel('Kaputelefon'),g=grid(p);
  g.append(check('Kaputelefon engedélyezése',o,'enabled',render));
  if(!o.enabled)return;
  input(g,'Kamera MJPEG-címe',o,'cameraUrl','url',{maxlength:256,placeholder:'http://192.168.1.50/mjpg/video.mjpg'});
  input(g,'Kamera felhasználóneve',o,'cameraUser','text',{maxlength:95}); secret(g,'Kamera jelszava','cameraPassword');
  g.append(field('Csengetés folyamatban',mappingInput(o,'uuid',app.controls,'uuid')),
    field('Kapunyitó parancs (opcionális)',mappingInput(o,'gateUuid',app.controls.filter(r=>eligible('pulse',r)),'control_uuid')));
  input(g,'Hangszóró hangereje (%)',o,'speakerVolume','number',{min:0,max:100});
  input(g,'Mikrofon erősítése (%)',o,'microphoneGain','number',{min:0,max:100});
  const tones={'':'Néma','tubular-chime.wav':'Csőharang'};
  if(o.ringtone && !tones[o.ringtone]) tones[o.ringtone]=o.ringtone;
  g.append(field('Csengőhang',select(o,'ringtone',tones)),check('Csengőhang ismétlése',o,'ringtoneRepeat'));
}
function render(){
  if(!app.profile)return;
  lists=new Map(); editor.replaceChildren();
  for(const b of document.querySelectorAll('[data-section]')){
    if(b.dataset.section===app.section)b.setAttribute('aria-current','page'); else b.removeAttribute('aria-current');
  }
  ({tabs:renderTabs,loxone:renderLoxone,display:renderDisplay,intercom:renderIntercom})[app.section]();
}
for(const b of document.querySelectorAll('[data-section]'))b.onclick=()=>{app.section=b.dataset.section;render();};
async function getJSON(url){
  const res=await fetch(url,{cache:'no-store'});if(!res.ok)throw Error(await res.text()||res.statusText);return res.json();
}
async function refreshControls(event){
  const b=event.currentTarget;b.disabled=true;
  const status=b.closest('.panel').querySelector('.hint');status.textContent='Miniserver-adatlista frissítése…';
  try{
    const url='/api/controls/refresh',request=await fetch(url,{method:'POST',headers:{'X-Panel-Request':'1'}});
    if(!request.ok)throw Error(await request.text());
    const deadline=Date.now()+45000;
    while((await getJSON(url)).refreshing){
      if(Date.now()>=deadline)throw Error('Az adatlista frissítése még folyamatban van. Próbáld újra később.');
      await new Promise(resolve=>setTimeout(resolve,1000));
    }
    await fetchControls();if(app.section==='loxone')render();
  }catch(e){status.textContent=e.message;status.classList.add('error');}finally{b.disabled=false;}
}
async function fetchControls(){
  const response=await getJSON('/api/controls');app.controls=response.controls||[];
  app.connected=!!response.connected;app.connectionStatus=response.status||'';
}
save.onclick=async()=>{
  app.busy=true;save.disabled=true;editor.inert=true;
  try{
    const body={...app.profile,...app.secrets};
    const res=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json','X-Panel-Request':'1'},body:JSON.stringify(body)});
    if(!res.ok)throw Error(await res.text());
    app.profile=await res.json();app.secrets={};app.dirty=false;say('Beállítások mentve az SD-kártyára.');render();
  }catch(e){say(e.message,true);}
  finally{app.busy=false;save.disabled=!app.dirty;editor.inert=false;}
};
window.addEventListener('beforeunload',event=>{if(app.dirty){event.preventDefault();event.returnValue='';}});
(async()=>{
  try{
    app.profile=await getJSON('/api/config');
    try{app.assets=await getJSON('/assets/catalog.json');}catch{ /* First boot has no asset pack yet. */ }
    try{await fetchControls();}catch{ /* Offline setup allows manual UUIDs. */ }
    say(app.connected?'Miniserver csatlakoztatva.':'Helyi konfiguráció');render();
  }catch(e){say('A beállítások nem tölthetők be. '+e.message,true);}
})();
