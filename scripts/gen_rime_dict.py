import sys
from pathlib import Path

def normalize_pinyin(pinyin: str):
    parts = [p.strip().lower() for p in pinyin.strip().split() if p.strip()]
    if not parts:
        return "", ""
    joined = "".join(parts)
    initials = "".join([p[0] for p in parts if p])
    return joined, initials

def escape_c(s: str):
    return s.replace('\\', '\\\\').replace('"', '\\"')

def main():
    if len(sys.argv) != 4:
        print("usage: gen_rime_dict.py <input.yaml> <min_freq> <output.h>")
        return 1
    in_path = Path(sys.argv[1])
    min_freq = int(sys.argv[2])
    out_path = Path(sys.argv[3])

    entries = {}
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
                # Sometimes there is no '...' line; detect first data line with tabs
                if '\t' not in line:
                    continue
            if '\t' not in line:
                continue
            parts = line.split('\t')
            if len(parts) < 2:
                continue
            phrase = parts[0].strip()
            pinyin = parts[1].strip()
            freq = 0
            if len(parts) >= 3:
                try:
                    freq = int(parts[2].strip())
                except ValueError:
                    freq = 0
            if freq <= min_freq:
                continue
            joined, initials = normalize_pinyin(pinyin)
            if not joined:
                continue
            key = (phrase, joined, initials)
            prev = entries.get(key)
            if prev is None or freq > prev:
                entries[key] = freq

    items = [ (k[1], k[2], k[0], v) for k, v in entries.items() ]
    items.sort(key=lambda x: x[3], reverse=True)

    out_lines = []
    out_lines.append("/**\n * @file lv_ime_phrase_dict.h\n *\n * Auto-generated from Rime dict. Do not edit manually.\n */\n")
    out_lines.append("#ifndef LV_IME_PHRASE_DICT_H\n#define LV_IME_PHRASE_DICT_H\n\n#include <stdint.h>\n\ntypedef struct {\n    const char * pinyin;\n    const char * initials;\n    const char * phrase;\n    uint32_t freq;\n} lv_ime_phrase_entry_t;\n\nstatic const lv_ime_phrase_entry_t ime_phrase_dict[] = {\n")
    for pinyin_joined, initials, phrase, freq in items:
        out_lines.append(f"    {{\"{escape_c(pinyin_joined)}\", \"{escape_c(initials)}\", \"{escape_c(phrase)}\", {freq}}},\n")
    out_lines.append("    {NULL, NULL, NULL, 0}\n};\n\n#endif /* LV_IME_PHRASE_DICT_H */\n")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(''.join(out_lines), encoding='utf-8')
    print(f"Wrote {len(items)} entries to {out_path}")
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
