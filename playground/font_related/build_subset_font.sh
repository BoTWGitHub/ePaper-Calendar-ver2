#!/bin/bash

# 檢查 pyftsubset 是否存在
if ! command -v pyftsubset &> /dev/null; then
    echo "❌ pyftsubset 尚未安裝，請先執行 install.sh"
    exit 1
fi

# === 確保必要的資料夾存在 ===
mkdir -p collection
mkdir -p subset_font

# === 步驟 1: 分析所有 .ics 檔案，產生 used_letters.txt ===
echo "🔍 收集 ICS 中使用的中文字..."
python3 grab_letter_from_ics.py ics collection/used_letters.txt
if [ $? -ne 0 ]; then
    echo "❌ 分析 ICS 檔案失敗，停止腳本執行"
    exit 1
fi

# === 步驟 2: 使用 pyftsubset 建立子集字型 ===
echo "✂️ 開始製作字型子集..."

for font_file in font/*.{ttf,otf}; do
    [ -e "$font_file" ] || continue  # 如果沒有符合的檔案就跳過

    filename=$(basename "$font_file")
    output_file="subset_font/subset_$filename"

    echo "  ➤ 子集化 $filename..."
    pyftsubset "$font_file" \
        --text-file=collection/used_letters.txt \
        --output-file="$output_file" \
        --layout-features='*' \
        --flavor=truetype \
        --unicodes='*' \
        --retain-gids \
        --symbol-cmap \
        --name-IDs='*' \
        --glyph-names

    if [ $? -eq 0 ]; then
        echo "    ✅ 完成：$output_file"
    else
        echo "    ❌ 失敗：$filename"
    fi
done

echo "✅ 全部完成！子集字型已輸出至 subset_font/"

