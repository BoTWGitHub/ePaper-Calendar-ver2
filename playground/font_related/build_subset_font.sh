#!/bin/bash

# 檢查 pyftsubset 是否存在
if ! command -v pyftsubset &> /dev/null; then
    echo "❌ pyftsubset 尚未安裝，請先執行 install.sh"
    exit 1
fi

# === 確保必要的資料夾存在 ===
mkdir -p collection
mkdir -p subset_font
mkdir -p output    # 🔧 為 xxd 輸出預先建立

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

    if [ $? -eq 0 ]; then
        echo "    ✅ 完成：$output_file"
    else
        echo "    ❌ 失敗：$filename"
    fi
done

echo "📦 開始轉換 subset_font/* 為 .c 檔案..."

# === 步驟 3: 使用 xxd -i 將 subset 字型轉成 C 陣列 ===
for subset in subset_font/*.ttf; do
    [ -e "$subset" ] || continue
    base=$(basename "$subset" .ttf)
    output_path="output/${base}.c"

    echo "  ➤ 產生 C 檔案：$output_path"
    xxd -i "$subset" | sed 's/^unsigned char /const unsigned char /' \
    | sed 's/^unsigned int /const unsigned int /' > "$output_path"

    if [ $? -eq 0 ]; then
        echo "    ✅ C 檔完成：$output_path"
    else
        echo "    ❌ 轉換失敗：$subset"
    fi
done

echo "🎉 全部完成！C 檔案已儲存至 output/"

