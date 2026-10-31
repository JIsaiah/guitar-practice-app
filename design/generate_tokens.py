"""design/tokens.json から、CSS の変数と C++ の定数を作る (実装指示 T1)

使い方 (リポジトリの直下で):
    python design/generate_tokens.py

作るもの:
    design/generated/tokens.css     試作の HTML (と、将来の練習画面) が読む CSS の変数
    Source/UI/DesignTokens.h        JUCE の画面が使う C++ の定数
    design/generated/contrast.md    文字と背景のコントラスト比の一覧

作ったあと、CSS と C++ を読み直して色の値が一致することと、文字のコントラスト比が 4.5:1 以上であることを確かめる。
問題があれば、終了コード 1 で終わる。標準ライブラリだけで動く。
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOKENS = ROOT / "design" / "tokens.json"
CSS_OUT = ROOT / "design" / "generated" / "tokens.css"
CPP_OUT = ROOT / "Source" / "UI" / "DesignTokens.h"
CONTRAST_OUT = ROOT / "design" / "generated" / "contrast.md"

HEADER = "このファイルは design/generate_tokens.py が design/tokens.json から作ります。直接編集しないでください"

# 文字として使う組み合わせ (前景, 背景, 必要な比, メモ)
TEXT_PAIRS = [
    ("text", "background", 4.5, ""),
    ("text", "surface", 4.5, ""),
    ("textMuted", "background", 4.5, ""),
    ("textMuted", "surface", 4.5, ""),
    ("onAccent", "accent", 4.5, "アクセントの塗りの上の文字"),
]
# 大きな文字・塗りにだけ使う組み合わせ (3:1 で足りる。WCAG の大きな文字の基準)
LARGE_PAIRS = [
    ("accent", "background", 3.0, "大きな文字と塗りにだけ使う"),
    ("accent", "surface", 3.0, "大きな文字と塗りにだけ使う"),
]


def kebab(name: str) -> str:
    return re.sub(r"([A-Z])", lambda m: "-" + m.group(1).lower(), name)


def parse_hex(value: str):
    """#RRGGBB または #RRGGBBAA → (r, g, b, a)。各 0〜255"""
    h = value.lstrip("#")
    if len(h) not in (6, 8):
        raise ValueError(f"色の形式が違います: {value}")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    a = int(h[6:8], 16) if len(h) == 8 else 255
    return r, g, b, a


def css_colour(value: str) -> str:
    r, g, b, a = parse_hex(value)
    if a == 255:
        return f"#{r:02X}{g:02X}{b:02X}"
    return f"rgba({r}, {g}, {b}, {a / 255:.3f})"


def juce_argb(value: str) -> str:
    r, g, b, a = parse_hex(value)
    return f"0x{a:02x}{r:02x}{g:02x}{b:02x}"


def luminance(value: str) -> float:
    def channel(c):
        c /= 255
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4

    r, g, b, _ = parse_hex(value)
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def contrast(a: str, b: str) -> float:
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


#===============================================================================
def make_css(tokens) -> str:
    out = [f"/* {HEADER} */", ""]

    def theme_block(theme):
        lines = [f"  --color-{kebab(k)}: {css_colour(v)};" for k, v in theme["color"].items()]
        s = theme["shadow"]
        lines.append(f"  --shadow: 0 {s['offsetY']}px {s['blur']}px {css_colour(s['color'])};")
        return lines

    # テーマに関係ない値
    out.append(":root {")
    out.append(f"  --font-ui: \"{tokens['font']['ui']}\", \"Segoe UI\", sans-serif;")
    out.append(f"  --font-numeric: \"{tokens['font']['numeric']}\", \"{tokens['font']['ui']}\", sans-serif;")
    for group, prefix in (("fontSize", "font-size"), ("space", "space"), ("radius", "radius"), ("size", "size")):
        for k, v in tokens[group].items():
            if not k.startswith("$"):
                out.append(f"  --{prefix}-{kebab(k)}: {v}px;")
    m = tokens["motion"]
    out.append(f"  --motion-duration: {m['durationMs']}ms;")
    out.append(f"  --motion-stagger: {m['staggerMs']}ms;")
    out.append(f"  --motion-distance: {m['distancePx']}px;")
    out.append(f"  --motion-max-total: {m['maxTotalMs']}ms;")
    out.append("}")
    out.append("")

    themes = tokens["themes"]

    # 既定はライト。OS がダークなら、data-theme が指定されていないときだけダーク
    out.append(":root, :root[data-theme=\"light\"] {")
    out += theme_block(themes["light"])
    out.append("}")
    out.append("")
    out.append("@media (prefers-color-scheme: dark) {")
    out.append("  :root:not([data-theme]) {")
    out += ["  " + line for line in theme_block(themes["dark"])]
    out.append("  }")
    out.append("}")
    out.append("")

    for name, theme in themes.items():
        if name == "light":
            continue
        out.append(f":root[data-theme=\"{name}\"] {{")
        out += theme_block(theme)
        out.append("}")
        out.append("")

    return "\n".join(out)


def make_cpp(tokens) -> str:
    colour_names = list(next(iter(tokens["themes"].values()))["color"].keys())

    out = [
        "#pragma once",
        "",
        f"// {HEADER}",
        "// 色・文字・余白などのデザイントークン (設計書 v0.7 4.7〜4.9)。画面の数値は、ここからだけ取る",
        "",
        "#include <juce_graphics/juce_graphics.h>",
        "",
        "namespace DesignTokens",
        "{",
        "    // テーマごとに変わる値。テーマは「この組」を1つ足せば増やせる",
        "    struct Theme",
        "    {",
        "        const char* name;",
    ]
    for name in colour_names:
        out.append(f"        juce::Colour {name};")
    out += [
        "        juce::Colour shadowColour;",
        "        int shadowOffsetY, shadowBlur;",
        "    };",
        "",
    ]

    theme_names = list(tokens["themes"].keys())
    for tname, theme in tokens["themes"].items():
        values = [f"juce::Colour ({juce_argb(theme['color'][c])})" for c in colour_names]
        s = theme["shadow"]
        values += [f"juce::Colour ({juce_argb(s['color'])})", str(s["offsetY"]), str(s["blur"])]
        out.append(f"    inline const Theme {tname} {{ \"{tname}\",")
        for i, v in enumerate(values):
            comment = f"  // {colour_names[i]}" if i < len(colour_names) else ""
            sep = "," if i < len(values) - 1 else " };"
            out.append(f"        {v}{sep}{comment}")
        out.append("")

    out.append("    inline const Theme* const allThemes[] { " + ", ".join(f"&{n}" for n in theme_names) + " };")
    out.append("")
    out.append("    // 名前からテーマを探す。見つからなければ最初のテーマ")
    out.append("    inline const Theme& findTheme (const juce::String& name)")
    out.append("    {")
    out.append("        for (auto* theme : allThemes)")
    out.append("            if (name == theme->name)")
    out.append("                return *theme;")
    out.append("        return *allThemes[0];")
    out.append("    }")
    out.append("")

    out.append("    namespace Font")
    out.append("    {")
    out.append(f"        inline constexpr const char* ui = \"{tokens['font']['ui']}\";")
    out.append(f"        inline constexpr const char* numeric = \"{tokens['font']['numeric']}\";  // 等幅の数字で使う")
    out.append("    }")
    out.append("")

    for group, ns in (("fontSize", "FontSize"), ("space", "Space"), ("radius", "Radius"), ("size", "Size")):
        out.append(f"    namespace {ns}")
        out.append("    {")
        for k, v in tokens[group].items():
            if not k.startswith("$"):
                out.append(f"        inline constexpr int {k} = {v};")
        out.append("    }")
        out.append("")

    m = tokens["motion"]
    out.append("    namespace Motion")
    out.append("    {")
    out.append(f"        inline constexpr int durationMs = {m['durationMs']};")
    out.append(f"        inline constexpr int staggerMs = {m['staggerMs']};")
    out.append(f"        inline constexpr int distancePx = {m['distancePx']};")
    out.append(f"        inline constexpr int maxTotalMs = {m['maxTotalMs']};")
    out.append("    }")
    out.append("}")
    out.append("")
    return "\n".join(out)


def make_contrast(tokens):
    rows, ok = [], True
    for tname, theme in tokens["themes"].items():
        c = theme["color"]
        for fg, bg, need, note in TEXT_PAIRS + LARGE_PAIRS:
            ratio = contrast(c[fg], c[bg])
            passed = ratio >= need
            ok &= passed
            rows.append(f"| {tname} | {fg} `{c[fg]}` | {bg} `{c[bg]}` | {ratio:.2f}:1 | {need}:1 | {'OK' if passed else 'NG'} | {note} |")

    text = "\n".join([
        f"<!-- {HEADER} -->",
        "# コントラスト比",
        "",
        "| テーマ | 文字 | 背景 | 比 | 基準 | 結果 | メモ |",
        "| --- | --- | --- | --- | --- | --- | --- |",
        *rows,
        "",
        "基準: 普通の文字は 4.5:1、大きな文字と塗りは 3:1 (WCAG 2.1 AA)。",
        "",
    ])
    return text, ok


#===============================================================================
def check_consistency(tokens) -> bool:
    """生成した CSS と C++ を読み直し、テーマの色の値が tokens.json と一致するか確かめる"""
    css = CSS_OUT.read_text(encoding="utf-8")
    cpp = CPP_OUT.read_text(encoding="utf-8")
    ok = True

    for tname, theme in tokens["themes"].items():
        block = re.search(r'\[data-theme="' + tname + r'"\] \{(.*?)\}', css, re.S)
        cpp_block = re.search(r"inline const Theme " + tname + r" \{(.*?)\};", cpp, re.S)

        for key, value in theme["color"].items():
            css_match = re.search(r"--color-" + kebab(key) + r": ([^;]+);", block.group(1)) if block else None
            cpp_match = re.search(r"juce::Colour \((0x[0-9a-f]{8})\),?\s*// " + key + r"\b", cpp_block.group(1)) if cpp_block else None

            css_value = css_match.group(1).strip() if css_match else None
            cpp_value = cpp_match.group(1) if cpp_match else None

            if css_value != css_colour(value) or cpp_value != juce_argb(value):
                print(f"  不一致: {tname}.{key} json={value} css={css_value} cpp={cpp_value}")
                ok = False

    return ok


def main() -> int:
    # Windows のコンソール (cp932 / cp1252) でも日本語を出せるように
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    tokens = json.loads(TOKENS.read_text(encoding="utf-8"))

    CSS_OUT.parent.mkdir(parents=True, exist_ok=True)
    CSS_OUT.write_text(make_css(tokens), encoding="utf-8", newline="\n")
    CPP_OUT.write_text(make_cpp(tokens), encoding="utf-8", newline="\n")

    contrast_text, contrast_ok = make_contrast(tokens)
    CONTRAST_OUT.write_text(contrast_text, encoding="utf-8", newline="\n")

    print(f"作成: {CSS_OUT.relative_to(ROOT)}")
    print(f"作成: {CPP_OUT.relative_to(ROOT)}")
    print(f"作成: {CONTRAST_OUT.relative_to(ROOT)}")

    consistent = check_consistency(tokens)
    print("CSS と C++ の色: " + ("一致" if consistent else "不一致あり"))
    print("コントラスト比: " + ("すべて基準以上" if contrast_ok else "基準未満あり (contrast.md を見てください)"))

    return 0 if consistent and contrast_ok else 1


if __name__ == "__main__":
    sys.exit(main())
