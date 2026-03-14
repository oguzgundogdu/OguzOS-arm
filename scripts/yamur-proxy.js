#!/usr/bin/env node
/*
 * Yamur Proxy — Web rendering proxy for OguzOS browser
 *
 * Renders real web pages using Puppeteer (headless Chrome) and serves
 * them as raw pixel data + clickable link maps over plain HTTP.
 *
 * Endpoints:
 *   GET /render?url=<url>&w=<width>&h=<height>&scroll=<y>
 *     → Returns binary response:
 *        Header (16 bytes):
 *          u32 width, u32 height, u32 link_count, u32 reserved
 *        Link map (link_count * 280 bytes each):
 *          i32 x, y, w, h (16 bytes) + url (264 bytes, null-padded)
 *        Pixel data (width * height * 4 bytes, XRGB8888)
 *
 * Usage:
 *   npm install puppeteer
 *   node yamur-proxy.js [port]
 *
 * Default port: 8088
 * OguzOS accesses this at http://10.0.2.2:8088 via QEMU user-mode networking
 */

const http = require('http');
const puppeteer = require('puppeteer');

const PORT = parseInt(process.argv[2] || '8088', 10);
const MAX_LINKS = 64;

let browser = null;

async function getBrowser() {
  if (!browser) {
    browser = await puppeteer.launch({
      headless: 'new',
      args: ['--no-sandbox', '--disable-setuid-sandbox'],
    });
  }
  return browser;
}

async function renderPage(url, viewW, viewH, scrollY) {
  const b = await getBrowser();
  const page = await b.newPage();

  await page.setViewport({ width: viewW, height: viewH });

  try {
    await page.goto(url, { waitUntil: 'networkidle2', timeout: 15000 });
  } catch (e) {
    // Timeout is OK, render what we have
  }

  // Scroll to requested position
  if (scrollY > 0) {
    await page.evaluate((y) => window.scrollTo(0, y), scrollY);
    await new Promise((r) => setTimeout(r, 200));
  }

  // Extract clickable links with their bounding boxes
  const links = await page.evaluate((maxLinks) => {
    const result = [];
    const anchors = document.querySelectorAll('a[href]');
    for (const a of anchors) {
      if (result.length >= maxLinks) break;
      const href = a.href;
      if (!href || href.startsWith('javascript:')) continue;
      const rect = a.getBoundingClientRect();
      if (rect.width === 0 || rect.height === 0) continue;
      if (rect.bottom < 0 || rect.top > window.innerHeight) continue;
      result.push({
        x: Math.round(rect.x),
        y: Math.round(rect.y),
        w: Math.round(rect.width),
        h: Math.round(rect.height),
        url: href,
      });
    }
    return result;
  }, MAX_LINKS);

  // Take screenshot as raw PNG, then convert to raw XRGB
  const pngBuf = await page.screenshot({ type: 'png' });

  await page.close();

  // Decode PNG to raw pixels using canvas-less approach
  // We'll use the sharp-less approach: just send PNG and let a simpler
  // format work. Actually, let's use a raw bitmap approach.

  // Re-take screenshot as raw pixel data via CDP
  const page2 = await b.newPage();
  await page2.setViewport({ width: viewW, height: viewH });
  try {
    await page2.goto(url, { waitUntil: 'networkidle2', timeout: 15000 });
  } catch (e) {}
  if (scrollY > 0) {
    await page2.evaluate((y) => window.scrollTo(0, y), scrollY);
    await new Promise((r) => setTimeout(r, 200));
  }

  // Use CDP to get raw RGBA pixels
  const cdp = await page2.createCDPSession();
  const { data } = await cdp.send('Page.captureScreenshot', {
    format: 'png',
  });
  await page2.close();

  // We need to decode PNG to raw pixels. Use built-in approach.
  // Let's use a canvas-based decode via page evaluation instead.
  const page3 = await b.newPage();
  await page3.setViewport({ width: viewW, height: viewH });
  await page3.setContent(`<canvas id="c" width="${viewW}" height="${viewH}"></canvas>`);

  const rawBase64 = await page3.evaluate(async (pngBase64, w, h) => {
    const img = new Image();
    await new Promise((resolve, reject) => {
      img.onload = resolve;
      img.onerror = reject;
      img.src = 'data:image/png;base64,' + pngBase64;
    });
    const canvas = document.getElementById('c');
    const ctx = canvas.getContext('2d');
    ctx.drawImage(img, 0, 0, w, h);
    const imageData = ctx.getImageData(0, 0, w, h);
    // Convert RGBA to base64 for transfer
    const bytes = imageData.data;
    let binary = '';
    for (let i = 0; i < bytes.length; i++) {
      binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary);
  }, data, viewW, viewH);

  await page3.close();

  const rgbaPixels = Buffer.from(rawBase64, 'base64');

  // Convert RGBA → XRGB8888 (swap R and B for framebuffer, set alpha to 0)
  const pixelCount = viewW * viewH;
  const xrgbBuf = Buffer.alloc(pixelCount * 4);
  for (let i = 0; i < pixelCount; i++) {
    const r = rgbaPixels[i * 4 + 0];
    const g = rgbaPixels[i * 4 + 1];
    const b = rgbaPixels[i * 4 + 2];
    // XRGB8888: 0x00RRGGBB
    xrgbBuf[i * 4 + 0] = b;
    xrgbBuf[i * 4 + 1] = g;
    xrgbBuf[i * 4 + 2] = r;
    xrgbBuf[i * 4 + 3] = 0;
  }

  // Build response: header + link map + pixels
  // Header: 4x u32 LE (width, height, link_count, reserved)
  const header = Buffer.alloc(16);
  header.writeUInt32LE(viewW, 0);
  header.writeUInt32LE(viewH, 4);
  header.writeUInt32LE(links.length, 8);
  header.writeUInt32LE(0, 12);

  // Link map: each entry = 16 bytes coords + 264 bytes url = 280 bytes
  const linkBuf = Buffer.alloc(links.length * 280);
  for (let i = 0; i < links.length; i++) {
    const off = i * 280;
    linkBuf.writeInt32LE(links[i].x, off + 0);
    linkBuf.writeInt32LE(links[i].y, off + 4);
    linkBuf.writeInt32LE(links[i].w, off + 8);
    linkBuf.writeInt32LE(links[i].h, off + 12);
    const urlBytes = Buffer.from(links[i].url, 'utf-8');
    urlBytes.copy(linkBuf, off + 16, 0, Math.min(urlBytes.length, 263));
  }

  return Buffer.concat([header, linkBuf, xrgbBuf]);
}

const server = http.createServer(async (req, res) => {
  const parsed = new URL(req.url, `http://localhost:${PORT}`);

  if (parsed.pathname === '/render') {
    const url = parsed.searchParams.get('url');
    const w = parseInt(parsed.searchParams.get('w') || '800', 10);
    const h = parseInt(parsed.searchParams.get('h') || '600', 10);
    const scroll = parseInt(parsed.searchParams.get('scroll') || '0', 10);

    if (!url) {
      res.writeHead(400);
      res.end('Missing url parameter');
      return;
    }

    console.log(`[yamur] Rendering: ${url} (${w}x${h}, scroll=${scroll})`);

    try {
      const data = await renderPage(url, w, h, scroll);
      res.writeHead(200, {
        'Content-Type': 'application/octet-stream',
        'Content-Length': data.length,
      });
      res.end(data);
      console.log(`[yamur] Done: ${data.length} bytes`);
    } catch (e) {
      console.error(`[yamur] Error: ${e.message}`);
      res.writeHead(500);
      res.end('Render failed: ' + e.message);
    }
  } else if (parsed.pathname === '/health') {
    res.writeHead(200);
    res.end('ok');
  } else {
    res.writeHead(404);
    res.end('Not found');
  }
});

server.listen(PORT, () => {
  console.log(`[yamur] Proxy running on http://localhost:${PORT}`);
  console.log(`[yamur] OguzOS connects via http://10.0.2.2:${PORT}`);
  console.log(`[yamur] Usage: /render?url=https://example.com&w=800&h=600`);
});

process.on('SIGINT', async () => {
  if (browser) await browser.close();
  process.exit(0);
});
