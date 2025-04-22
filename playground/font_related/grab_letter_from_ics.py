"""Original script was made by GPT-4o"""

import sys
import re
import os
from collections import Counter

def extract_chinese(text):
    """只抓常見中文字與中標點符號"""
    return ''.join(re.findall(r'[\u4e00-\u9fff，。！？：「」、（）《》～．．…·°℃]', text))

def parse_ics(filename):
    with open(filename, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    buffer = ''
    for line in lines:
        line = line.strip()
        if line.startswith(('SUMMARY', 'DESCRIPTION', 'LOCATION')):
            content = re.sub(r'^[A-Z-]+.*?:', '', line)
            buffer += content + '\n'
    return buffer

def load_existing_chars(file_path):
    """從既有檔案中讀取已儲存的字元"""
    if not os.path.exists(file_path):
        return set()
    with open(file_path, 'r', encoding='utf-8') as f:
        return set(line.strip() for line in f if line.strip())

def main():
    if len(sys.argv) < 2:
        print("用法: python extract_chars_from_ics.py your_calendar.ics")
        sys.exit(1)

    filename = sys.argv[1]
    ics_text = parse_ics(filename)
    extracted = extract_chinese(ics_text)
    new_chars = set(extracted)

    existing_chars = load_existing_chars("used_chars.txt")
    combined_chars = sorted(existing_chars.union(new_chars))

    with open("used_chars.txt", "w", encoding="utf-8") as f:
        for ch in combined_chars:
            f.write(ch + '\n')

    print(f"✅ 累加後總共 {len(combined_chars)} 個字元已儲存於 used_chars.txt")

if __name__ == "__main__":
    main()

