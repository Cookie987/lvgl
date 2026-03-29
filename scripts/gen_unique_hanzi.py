import sys
from pathlib import Path


def is_cjk(ch: str) -> bool:
    cp = ord(ch)
    return (
        0x4E00 <= cp <= 0x9FFF or
        0x3400 <= cp <= 0x4DBF or
        0x20000 <= cp <= 0x2A6DF or
        0x2A700 <= cp <= 0x2B73F or
        0x2B740 <= cp <= 0x2B81F or
        0x2B820 <= cp <= 0x2CEAF or
        0x2CEB0 <= cp <= 0x2EBEF or
        0xF900 <= cp <= 0xFAFF or
        0x2F800 <= cp <= 0x2FA1F
    )


def main():
    if len(sys.argv) != 3:
        print("usage: gen_unique_hanzi.py <input.yaml> <output.txt>")
        return 1
    in_path = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    chars = set()
    in_body = False
    with in_path.open('r', encoding='utf-8') as f:
        for raw in f:
            line = raw.rstrip('\n')
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
            if len(parts) < 1:
                continue
            phrase = parts[0].strip()
            if not phrase:
                continue
            for ch in phrase:
                if is_cjk(ch):
                    chars.add(ch)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(''.join(sorted(chars)), encoding='utf-8')
    print(f"Wrote {len(chars)} unique Hanzi to {out_path}")
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
