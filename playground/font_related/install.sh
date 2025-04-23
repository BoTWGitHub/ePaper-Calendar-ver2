#!/bin/bash

echo "🔧 安裝必要工具..."

# 安裝 fonttools（包含 pyftsubset）
pip install -r requirements.txt

# 選用：確認 python3 是否存在
if ! command -v python3 &> /dev/null; then
    echo "❌ 請先安裝 python3"
    exit 1
fi

# 選用：提醒建立虛擬環境
echo "✅ 依賴安裝完成（建議使用 virtualenv 管理 Python 套件）"

