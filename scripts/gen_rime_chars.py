import sys
from pathlib import Path

def escape_c(s: str) -> str:
    return s.replace('\\', '\\\\').replace('"', '\\"')

def main():
    if len(sys.argv) != 3:
        print("usage: gen_rime_chars.py <input.yaml> <output.h>")
        return 1
    in_path = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    data = {}
    in_body = False
    with in_path.open('r', encoding='utf-8') as f:
        for raw in f:
            line = raw.strip('\n')
            if not line:
                continue
            s = line.strip()
            if s.startswith('#'):
                continue
            if s == '...':
                in_body = True
                continue
            if not in_body:
                if '\t' not in line:
                    continue
            if '\t' not in line:
                continue
            parts = line.split('\t')
            if len(parts) < 2:
                continue
            phrase = parts[0].strip()
            pinyin = parts[1].strip().lower()
            if not phrase or not pinyin:
                continue
            # only keep single-character entries
            if len(phrase) != 1:
                continue
            freq = 0
            if len(parts) >= 3:
                try:
                    freq = int(parts[2].strip())
                except ValueError:
                    freq = 0
            m = data.setdefault(pinyin, {})
            prev = m.get(phrase)
            if prev is None or freq > prev:
                m[phrase] = freq

    # build output lines sorted by pinyin for stability
    out_lines = []
    out_lines.append("/**\n * @file lv_ime_dict.h\n * @author Cookie987\n * @brief Pinyin to Chinese dictionary for LVGL's built-in Chinese IME keyboard mode.\n */\n\n")
    out_lines.append("#include <stdlib.h>\n#include <ctype.h>\n#include <string.h>\n\n")
    out_lines.append("typedef struct {\n    const char * pinyin;\n    const char * chinese;\n} pinyin_entry_t;\n\n")
    out_lines.append("static const pinyin_entry_t pinyin_dict[] = {\n")

    for pinyin in sorted(data.keys()):
        items = data[pinyin]
        # sort by freq desc, then by char
        chars = sorted(items.items(), key=lambda x: (-x[1], x[0]))
        chinese = ''.join([c for c, _ in chars])
        out_lines.append(f"    {{\"{escape_c(pinyin)}\", \"{escape_c(chinese)}\"}},\n")

    out_lines.append("    {NULL, NULL}\n};\n")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(''.join(out_lines), encoding='utf-8')
    print(f"Wrote {len(data)} pinyin entries to {out_path}")
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
