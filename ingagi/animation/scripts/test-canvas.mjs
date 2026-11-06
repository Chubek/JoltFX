// Real Canvas pixels and controls; uses the repository's Playwright installation.
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
const moduleURL = process.env.INGAGI_PLAYWRIGHT
    ? pathToFileURL(resolve(process.env.INGAGI_PLAYWRIGHT, 'index.mjs')).href
    : new URL('../../../frontends/web/node_modules/playwright/index.mjs', import.meta.url).href;
const {firefox} = await import(moduleURL);
const browser = await firefox.launch({headless: true});
try {
    const page = await browser.newPage();
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto(pathToFileURL(resolve(process.argv[2])).href);
    const pixels=await page.evaluate(()=>{
        const p=globalThis.ingagiAnimation;p.pause();p.seek(0);
        const ctx=document.querySelector('canvas').getContext('2d');
        const pixel=(x,y)=>Array.from(ctx.getImageData(x,y,1,1).data);
        const first=pixel(25,25);p.seek(1);
        return {first,old:pixel(25,25),moved:pixel(45,25),frame:p.frame};
    });
    assert.deepEqual(pixels,{first:[255,0,0,128],old:[0,0,0,0],moved:[255,0,0,128],frame:1});
    await page.locator('#seek').fill('0');await page.locator('#seek').dispatchEvent('input');
    assert.equal(await page.evaluate(()=>ingagiAnimation.frame),0);
    await page.locator('#play').click();
    await page.waitForFunction(()=>ingagiAnimation.frame===1);
    await page.evaluate(()=>ingagiAnimation.pause());
    assert.deepEqual(errors,[]);
    console.log('Canvas alpha, cleared pixels, frame timing, seek and playback passed');
} finally {await browser.close();}
