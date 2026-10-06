// tools/gen_font_day.mjs —— 生成 96px 大日数字字体（仅 - 与 0-9）
// 用法：node gen_font_day.mjs
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import fs from 'node:fs';

const here = path.dirname(fileURLToPath(import.meta.url));
const ttf = process.argv[2] || path.join(here, 'NotoSansSC.ttf');
const outDir = path.join(here, '..', 'main', 'assets', 'fonts');

if (!fs.existsSync(ttf)) {
    console.error('找不到字体 TTF:', ttf);
    process.exit(1);
}

const bin = path.join(here, 'node_modules', 'lv_font_conv', 'lv_font_conv.js');
const out = path.join(outDir, 'font_day_96.c');

const args = [
    bin,
    '--font', ttf,
    '--size', '96',
    '--bpp', '4',
    '--format', 'lvgl',
    '--no-compress',
    '--range', '0x2D,0x30-0x39',   // - 0123456789
    '-o', out,
];
console.log('生成 font_day_96.c …');
execFileSync(process.execPath, args, { stdio: 'inherit' });
console.log('完成:', out, fs.statSync(out).size, 'bytes');
