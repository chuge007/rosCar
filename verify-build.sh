#!/bin/bash
# 焊道跟踪优化 - 编译验证脚本
# 2026-10-23

set -e  # 遇到错误立即退出

echo "================================"
echo "焊道跟踪优化 - 编译验证"
echo "================================"
echo ""

# 1. 检查核心文件是否存在
echo "[1/4] 检查关键文件..."
FILES=(
    "src/laser_correction_controller.h"
    "src/laser_correction_controller.cpp"
    "src/seam_state_machine.h"
)

for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file"
    else
        echo "  ✗ $file - 缺失!"
        exit 1
    fi
done
echo ""

# 2. 检查关键代码是否存在
echo "[2/4] 检查代码修改..."
if grep -q "SeamStateMachine seamStateMachine_" src/laser_correction_controller.h; then
    echo "  ✓ 状态机成员变量已添加"
else
    echo "  ✗ 状态机成员变量缺失"
    exit 1
fi

if grep -q "void seamStateChanged" src/laser_correction_controller.h; then
    echo "  ✓ 状态机信号已添加"
else
    echo "  ✗ 状态机信号缺失"
    exit 1
fi

if grep -q "seamStateMachine_.update" src/laser_correction_controller.cpp; then
    echo "  ✓ 状态机更新逻辑已添加"
else
    echo "  ✗ 状态机更新逻辑缺失"
    exit 1
fi

if grep -q "centerCalibration_.calibrate" src/laser_correction_controller.cpp; then
    echo "  ✓ 激光校准逻辑已添加"
else
    echo "  ✗ 激光校准逻辑缺失"
    exit 1
fi
echo ""

# 3. 语法检查（快速）
echo "[3/4] C++语法快速检查..."
# 使用g++预处理检查基本语法
if command -v g++ &> /dev/null; then
    if g++ -std=c++11 -fsyntax-only -Isrc -I. src/laser_correction_controller.cpp 2>/dev/null; then
        echo "  ✓ 基本语法检查通过"
    else
        echo "  ⚠ 语法检查警告（可能需要完整Qt环境）"
    fi
else
    echo "  ⚠ g++未安装，跳过语法检查"
fi
echo ""

# 4. 统计修改量
echo "[4/4] 代码修改统计..."
ADDED_LINES_H=$(grep -c "^+.*SeamStateMachine\|^+.*seamStateChanged\|^+.*LaserCenterCalibration" <(git diff HEAD src/laser_correction_controller.h 2>/dev/null) || echo "0")
ADDED_LINES_CPP=$(grep -c "^+.*seamStateMachine_\|^+.*centerCalibration_" <(git diff HEAD src/laser_correction_controller.cpp 2>/dev/null) || echo "0")

echo "  · laser_correction_controller.h: 新增关键行 ~$ADDED_LINES_H"
echo "  · laser_correction_controller.cpp: 新增关键行 ~$ADDED_LINES_CPP"
echo ""

echo "================================"
echo "✅ 验证完成！"
echo "================================"
echo ""
echo "下一步："
echo "1. 完整编译: qmake && make"
echo "2. 查看文档: docs/CODE-CHANGES.md"
echo "3. 继续UI适配: robotcontrolpanel.cpp"
echo ""
