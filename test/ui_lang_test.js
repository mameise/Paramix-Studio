// Language switch: every tab must look the same after switching to German as when the page starts in German.
// Usage: node test/ui_lang_test.js <rom> [<rom> ...]
const { chromium } = require('playwright');
const path = require('path');
const TABS = ['figs', 'all', 'dim', 'emu', 'doom', 'feld', 'save'];
async function grab(page) {
  const out = {};
  out.start = await page.evaluate(() => document.body.innerText);
  for (const tb of TABS) {
    await page.click('.tab[data-tab="' + tb + '"]'); await page.waitForTimeout(150);
    out[tb] = await page.evaluate((k) => {
      const keep = (el) => el && !el.hidden && el.offsetParent !== null;
      const parts = [document.getElementById('pane-' + k).innerText, document.getElementById('plan').innerText];
      document.querySelectorAll('[title]').forEach((e) => { if (keep(e)) parts.push('title:' + e.title); });
      return parts.join('\n');
    }, tb);
  }
  return out;
}
async function open(browser, rom, lang) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.addInitScript((l) => { try { localStorage.setItem('paramix-lang', l); } catch (e) {} }, lang);
  await page.goto('file://' + path.resolve(__dirname, '../dist/ParamixStudio.html'));
  await page.setInputFiles('#fileRom', rom);
  await page.waitForSelector('#work.on', { timeout: 60000 });
  await page.waitForTimeout(500);
  return { page, errors };
}
const EN = /\b(the|this|with|and|from|saves?|choose|start|back|hold|device|button|loaded|room|not|only)\b/i;
(async () => {
  const browser = await chromium.launch();
  let bad = 0;
  for (const rom of process.argv.slice(2)) {
    const a = await open(browser, rom, 'en');
    await a.page.click('[data-lang="de"]'); await a.page.waitForTimeout(300);
    const switched = await grab(a.page);
    const b = await open(browser, rom, 'de');
    const fresh = await grab(b.page);
    for (const k of Object.keys(fresh)) {
      if (k === 'start') continue;
      const s = switched[k].split('\n'), f = new Set(fresh[k].split('\n'));
      const diff = s.filter((l) => l.trim() && !f.has(l));
      if (diff.length) { bad++; console.log('[' + path.basename(rom) + '] ' + k + ' stale after switch:\n  ' + diff.join('\n  ')); }
      const eng = fresh[k].split('\n').filter((l) => EN.test(l) && !/^title:/.test(l));
      if (eng.length) console.log('[' + path.basename(rom) + '] ' + k + ' looks English in DE:\n  ' + eng.join('\n  '));
    }
    const all = a.errors.concat(b.errors); if (all.length) { bad++; console.log('errors', all); }
    await a.page.close(); await b.page.close();
  }
  await browser.close();
  console.log(bad ? 'FAIL ' + bad : 'ok');
})();
