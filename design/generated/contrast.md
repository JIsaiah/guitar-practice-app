<!-- このファイルは design/generate_tokens.py が design/tokens.json から作ります。直接編集しないでください -->
# コントラスト比

| テーマ | 文字 | 背景 | 比 | 基準 | 結果 | メモ |
| --- | --- | --- | --- | --- | --- | --- |
| light | text `#1E2630` | background `#EEF2F6` | 13.57:1 | 4.5:1 | OK |  |
| light | text `#1E2630` | surface `#F8FAFC` | 14.60:1 | 4.5:1 | OK |  |
| light | textMuted `#5A6573` | background `#EEF2F6` | 5.27:1 | 4.5:1 | OK |  |
| light | textMuted `#5A6573` | surface `#F8FAFC` | 5.66:1 | 4.5:1 | OK |  |
| light | onAccent `#F8FAFC` | accent `#B45309` | 4.80:1 | 4.5:1 | OK | アクセントの塗りの上の文字 |
| light | accent `#B45309` | background `#EEF2F6` | 4.46:1 | 3.0:1 | OK | 大きな文字と塗りにだけ使う |
| light | accent `#B45309` | surface `#F8FAFC` | 4.80:1 | 3.0:1 | OK | 大きな文字と塗りにだけ使う |
| dark | text `#E2E4E7` | background `#121315` | 14.59:1 | 4.5:1 | OK |  |
| dark | text `#E2E4E7` | surface `#1B1C1F` | 13.38:1 | 4.5:1 | OK |  |
| dark | textMuted `#9A9FA6` | background `#121315` | 6.98:1 | 4.5:1 | OK |  |
| dark | textMuted `#9A9FA6` | surface `#1B1C1F` | 6.40:1 | 4.5:1 | OK |  |
| dark | onAccent `#121315` | accent `#F0A43A` | 8.92:1 | 4.5:1 | OK | アクセントの塗りの上の文字 |
| dark | accent `#F0A43A` | background `#121315` | 8.92:1 | 3.0:1 | OK | 大きな文字と塗りにだけ使う |
| dark | accent `#F0A43A` | surface `#1B1C1F` | 8.18:1 | 3.0:1 | OK | 大きな文字と塗りにだけ使う |

基準: 普通の文字は 4.5:1、大きな文字と塗りは 3:1 (WCAG 2.1 AA)。
