// tools/gen_font.mjs —— 生成 GB2312 中文 LVGL 字体（16px 与 24px）
//
// 字符集：ASCII 0x20-0x7E + GB2312 全部汉字（区 16-87，6763 字，含二级字库如"闫"）+ 常用中文符号。
// 用法：node gen_font.mjs <ttf路径> <输出目录>
// 依赖：npm i（安装 lv_font_conv）
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import fs from 'node:fs';

const here = path.dirname(fileURLToPath(import.meta.url));
const ttf = process.argv[2] || path.join(here, 'NotoSansSC.ttf');
const outDir = process.argv[3] || path.join(here, '..', 'main', 'assets', 'fonts');

if (!fs.existsSync(ttf)) {
    console.error('找不到字体 TTF:', ttf);
    process.exit(1);
}
fs.mkdirSync(outDir, { recursive: true });

// ---- 收集字符集 ----
const dec = new TextDecoder('gb2312');
const set = new Set();

// ASCII 可见字符（0x20-0x7E 由 --range 提供，这里也加入集合做统计）
for (let c = 0x20; c <= 0x7e; c++) set.add(String.fromCharCode(c));

// GB2312 全部汉字：区 16-87（一级 16-55 + 二级 56-87），位 1-94（过滤 PUA 私有区映射）
for (let qu = 16; qu <= 87; qu++) {
    for (let wei = 1; wei <= 94; wei++) {
        const bytes = new Uint8Array([0xA0 + qu, 0xA0 + wei]);
        const ch = dec.decode(bytes);
        const cp = ch ? ch.codePointAt(0) : 0;
        if (ch && ch !== '\uFFFD' && ch.length === 1
            && cp >= 0x20 && !(cp >= 0xE000 && cp <= 0xF8FF)) {
            set.add(ch);
        }
    }
}

// 常用中文标点/符号（GB2312 符号区冗余太多，只挑常用的）
const extra = '，。、；：？！“”‘’（）《》〈〉【】…—·～％℃＋－×÷〇';
for (const ch of extra) set.add(ch);

const symbols = [...set].join('');
console.log(`字符总数: ${set.size}（含 ASCII）`);

// ---- 生成 ----
// 注意：Node>=20 不允许直接 spawn .cmd（EINVAL），改跑 JS 入口。
const bin = path.join(here, 'node_modules', 'lv_font_conv', 'lv_font_conv.js');

function gen(size, name) {
    const args = [
        bin,
        '--font', ttf,
        '--size', String(size),
        '--bpp', '4',
        '--format', 'lvgl',
        '--no-compress',
        '--range', '0x20-0x7E',
        '--symbols', symbols,
        '-o', path.join(outDir, name),
    ];
    console.log(`生成 ${name} (${size}px) …`);
    execFileSync(process.execPath, args, { stdio: 'inherit' });
    const st = fs.statSync(path.join(outDir, name));
    console.log(`  完成: ${(st.size / 1024).toFixed(0)} KB`);
}

gen(16, 'font_cn_16.c');
gen(24, 'font_cn_24.c');
console.log('全部字体已生成到', outDir);
