// node test.mjs BROWSER URL BATCH: runs BATCH with /c in the page mcpp.dist.web
// packs from mcpp/web.html, prints its standard output (check.sh grades it), and
// checks that a file outlives the run that wrote it.
import { readFileSync } from 'node:fs';
import { chromium } from 'playwright-core';

const [exe, url, batch] = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: exe, headless: true });
const page = await browser.newPage();
page.on('pageerror', (e) => console.error('pageerror:', e.message));
await page.goto(url);
await page.waitForFunction(() => window.cmdReady);
const text = readFileSync(batch, 'utf8');
const run = await page.evaluate((t) => runCmd(['/c', 'test.bat'], { 'test.bat': t }), text);
for (const l of run.lines) if (!l.startsWith('\u0000')) console.log(l);
await page.evaluate(() => runCmd(['/c', 'echo kept> persist.txt']));
const back = await page.evaluate(() => runCmd(['/c', 'type persist.txt']));
const kept = back.lines.includes('kept');
console.error(`browser ${browser.version()}: status ${run.status}; a file carried between runs: ${kept}`);
await browser.close();
process.exit(kept ? 0 : 1);
