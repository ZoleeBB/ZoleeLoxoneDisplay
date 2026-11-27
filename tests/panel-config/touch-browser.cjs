// Run with Playwright installed: CHROME_PATH=/path/to/chrome node tests/panel-config/touch-browser.cjs
// Set TOUCH_SCREENSHOT=/path/to/output.png to capture the actual firmware web editor with test data.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const root = path.resolve(__dirname, '../..');
const web = path.join(root, 'main/web');
const uuid = n => '0f0c0000-1234-5678-' + String(n).padStart(16,'0');
const [light, otherLight, player, blind, latch, pulse, virtual, secured, centralBlind] = Array.from({length:9},(_,i)=>uuid(i+1));
const savedMissing = uuid(91);
const empty = () => ({type:'', uuid:'', action:'', sceneId:0});
const row = (id,type,name,eligibility) => ({uuid:id,control_uuid:id,type,name,room:'Nappali',secured:false,...eligibility});
const rows = [
  row(light,'LightControllerV2','Mennyezeti fény',{touch_target:true,lighting_blocked:false}),
  row(otherLight,'LightControllerV2','Olvasósarok',{touch_target:true,lighting_blocked:false}),
  row(player,'AudioZoneV2','Nappali hangszóró',{touch_target:true,controls_type:'audio',controls_blocked:false}),
  row(blind,'Jalousie','Teraszárnyékoló',{touch_target:true,shading_blocked:false}),
  row(centralBlind,'CentralJalousie','Központi árnyékolás',{touch_target:true,shading_blocked:false}),
  row(latch,'Switch','Állólámpa',{touch_switch:true}),row(pulse,'Pushbutton','Csengő',{touch_button:true}),
  row(virtual,'Pushbutton','Touch virtuális bemenet',{touch_button:true,touch_input:true}),
  row(uuid(80),'LightControllerV2','Hiányos állapotlista',{lighting_blocked:false}),
  row(secured,'LightControllerV2','Zárolt vezérlő',{touch_target:true,lighting_blocked:false,secured:true})
];
const profileWithoutTouch = {
  schema:1,revision:1,adminUser:'',loxone:{host:'mini',user:'panel',port:80,tls:false,ca_pem:''},
  display:{saverSeconds:15,dayBrightness:65,nightBrightness:15,nightMode:'timer',nightStart:'22:00',nightEnd:'06:00',nightUuid:''},
  tabs:[],intercom:{enabled:false}
};

(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_PATH||undefined,
    args:['--no-sandbox','--disable-dev-shm-usage','--disable-gpu']});
  try{
    const page=await browser.newPage({viewport:{width:1280,height:920},deviceScaleFactor:1});
    page.setDefaultTimeout(5000);
    let profile=structuredClone(profileWithoutTouch), writes=0, loads=0;
    const errors=[],requested=[];
    page.on('pageerror',e=>errors.push(e.message));
    await page.route('http://panel.test/**',async route=>{
      const req=route.request(),url=new URL(req.url());
      if(url.pathname==='/api/config'){
        if(req.method()==='POST'){
          assert.equal(req.headers()['x-panel-request'],'1');profile=req.postDataJSON();++profile.revision;++writes;
        }
        return route.fulfill({json:profile});
      }
      if(url.pathname==='/api/controls')return route.fulfill({json:{connected:true,status:'Kapcsolódva.',controls:rows}});
      if(url.pathname==='/assets/catalog.json')return route.fulfill({json:{backgrounds:[],icons:[]}});
      if(url.pathname==='/api/touch/moods'){
        const id=url.searchParams.get('uuid');requested.push(id);
        assert([light,otherLight].includes(id));
        if(++loads===1)return route.fulfill({json:{status:'loading',moods:[]}});
        return route.fulfill({json:{status:'ready',moods:id===light?[{id:4,name:'Olvasás'},{id:8,name:'Esti fény'}]:[{id:22,name:'Munka'}]}});
      }
      const f={'/':'index.html','/admin.js':'admin.js','/admin.css':'admin.css'}[url.pathname];
      if(f)return route.fulfill({path:path.join(web,f)});
      return route.fulfill({status:404});
    });
    const touchTab=page.getByRole('button',{name:'Használat Touch-ként',exact:true});
    const save=page.getByRole('button',{name:'Mentés',exact:true});
    const toggle=page.getByRole('checkbox',{name:'Érintési zónák engedélyezése a képernyőkímélőn'});
    const center=page.getByRole('combobox',{name:'Középső világításvezérlő',exact:true});
    const gap=page.getByRole('spinbutton',{name:'Koppintások közötti megengedett szünet (mp)'});
    const zones=page.locator('fieldset.touch-zone');
    const zoneField=(i,label)=>zones.nth(i).getByRole('combobox',{name:label,exact:true});
    const selectType=async(i,type,id,action)=>{
      await zoneField(i,'Funkció').selectOption(type);
      await zoneField(i,'Vezérlő').selectOption(id);
      if(action)await zoneField(i,'Művelet').selectOption(action);
    };
    await page.goto('http://panel.test/');await touchTab.click();
    assert.equal(await zones.count(),4);assert.equal(await toggle.isChecked(),false);
    assert.equal(await gap.inputValue(),'0.4');assert.equal(writes,0);
    profile.display.saverSeconds=0;await page.reload();await touchTab.click();
    await page.getByRole('button',{name:'Kijelző beállításai'}).click();
    await page.getByRole('heading',{name:'Fényerő és képernyőkímélő'}).waitFor();
    profile.display.saverSeconds=15;await page.reload();await touchTab.click();
    await toggle.check();
    await zoneField(0,'Funkció').selectOption('lighting');
    await save.click();await page.getByText('Bal felső sarok: válassz vezérlőt.').waitFor();
    assert.equal(writes,0);
    await zoneField(0,'Vezérlő').selectOption(light);
    await zoneField(0,'Művelet').selectOption('scene');
    await zoneField(0,'Világítási kép').getByRole('option',{name:'Esti fény'}).waitFor({state:'attached'});
    await save.click();await page.getByText('Bal felső sarok: válassz világítási képet.').waitFor();assert.equal(writes,0);
    await zoneField(0,'Világítási kép').selectOption('8');
    await selectType(1,'audio',player,'volumeUp');
    assert.deepEqual(await zoneField(1,'Művelet').locator('option').evaluateAll(opts=>opts.map(o=>o.value)),
      ['volumeUp','volumeDown','next','previous','playPause']);
    await selectType(2,'shading',centralBlind,'down');
    assert.equal(await zoneField(2,'Vezérlő').locator('option').count(),3);
    await selectType(3,'pulse',pulse,'trigger');
    await center.selectOption(light);
    assert.equal(await center.locator('option').count(),3);
    await gap.fill('0.125');await save.click();assert.equal(writes,0);
    await gap.fill('0.425');await save.click();await page.getByText('Beállítások mentve az SD-kártyára.').waitFor();
    const valid=structuredClone(profile.touch);
    assert.deepEqual(valid,{enabled:true,tapWindowMs:425,corners:[
      {type:'lighting',uuid:light,action:'scene',sceneId:8},{type:'audio',uuid:player,action:'volumeUp',sceneId:0},
      {type:'shading',uuid:centralBlind,action:'down',sceneId:0},{type:'pulse',uuid:pulse,action:'trigger',sceneId:0}],
      center:{uuid:light,inputUuid:''}});
    assert.equal(profile.tabs.length,0);assert(requested.includes(light));
    if(process.env.TOUCH_SCREENSHOT)await page.screenshot({path:process.env.TOUCH_SCREENSHOT,fullPage:true});
    await selectType(3,'switch',latch,'trigger');
    await zones.nth(3).getByText('Loxone Configban bekötött Tr bemenet',{exact:true}).click();
    const trigger=page.getByRole('combobox',{name:'Virtuális nyomógomb a Tr bemenethez'});
    assert.equal(await trigger.locator('option').count(),2);await trigger.selectOption(virtual);
    await page.getByText('Loxone Configban bekötött M+ bemenet',{exact:true}).click();
    const input=page.getByRole('combobox',{name:'Virtuális nyomógomb az M+ bemenethez'});
    assert.equal(await input.locator('option').count(),2);await input.selectOption(virtual);
    await toggle.uncheck();await save.click();await page.getByText('Beállítások mentve az SD-kártyára.').waitFor();
    assert.equal(profile.touch.corners[3].inputUuid,virtual);assert.equal(profile.touch.center.inputUuid,virtual);
    await zoneField(0,'Vezérlő').selectOption(otherLight);
    await zoneField(0,'Világítási kép').getByRole('option',{name:'Munka'}).waitFor({state:'attached'});
    assert.equal(await zoneField(0,'Világítási kép').inputValue(),'');
    assert.equal(await zoneField(0,'Világítási kép').getByRole('option',{name:'Esti fény'}).count(),0);
    await center.selectOption(otherLight);
    await page.getByText('Loxone Configban bekötött M+ bemenet',{exact:true}).click();
    assert.equal(await input.inputValue(),'');
    // Offline edits to unrelated settings must retain the configured identity.
    profile.touch.corners[3]={...empty(),type:'switch',uuid:savedMissing,action:'trigger'};
    profile.touch.corners[0].sceneId=42;
    await page.reload();await touchTab.click();
    assert.equal(await zoneField(3,'Vezérlő').inputValue(),savedMissing);
    await page.getByText('A mentett világítási kép már nincs a blokk listájában. Válassz újat.').waitFor();
    assert.equal(await zoneField(0,'Világítási kép').inputValue(),'42');
    await zoneField(0,'Világítási kép').selectOption('4');
    assert.equal(await page.getByText('A mentett világítási kép már nincs a blokk listájában. Válassz újat.').count(),0);
    await page.setViewportSize({width:390,height:844});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    assert.deepEqual(errors,[]);
    console.log('Touch browser checks passed: legacy profile, all zone assignments, filtered controllers, live moods, validation, Tr/M+ bindings, saved/offline state and mobile layout.');
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
