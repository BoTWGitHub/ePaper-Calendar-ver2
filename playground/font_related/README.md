# 📁 字型子集建立工具 for 文字日曆裝置

這個專案會：
1. 從指定資料夾內的 `.ics` 行事曆檔案擷取中文文字
2. 整理出實際使用到的中文字清單
3. 使用 `pyftsubset` 針對字型檔建立精簡版的子集字型

適用於：
- ESP32 / e-paper 顯示器
- 任意僅需少量文字的字型優化應用場景

---
## 📁 檔案結構  
.
├── ics/                 # 放入 .ics 檔案
├── font/                # 放入原始字型 (.ttf/.otf)
├── collection/          # 中繼資料儲存區（輸出字清單）
├── subset_font/         # 子集字型輸出位置
├── grab_letter_from_ics.py
├── build_font_subset.sh
├── install.sh 
├── requirements.txt
├── README.md

## 📦 安裝環境

建議使用 Python 3.8+ 搭配虛擬環境。

```bash
python3 -m venv venv
source venv/bin/activate
source install.sh
source build_subset_font.sh
