# Git操作指南

## 1. 强制推送到GitHub

当前分支已提交所有更改（包括触摸屏修复）。执行以下命令推送到GitHub：

```bash
cd D:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchTmp
git push github main:PA1664WorkbenchTmp --force
```

**说明**：
- 目标分支：`PA1664WorkbenchTmp`
- 使用 `--force` 强制推送（覆盖远程历史）
- 远程仓库：https://github.com/chuge007/rosCar

---

## 2. 从Gitee拉取超声相关更新

### 步骤1：查看远程更新

```bash
cd D:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchTmp
git fetch origin main
```

### 步骤2：查看更新内容

```bash
# 查看更新摘要
git log HEAD..origin/main --oneline

# 查看超声相关的更新
git log HEAD..origin/main --oneline --grep="ultrasonic\|超声"

# 查看文件变更
git diff HEAD..origin/main --stat
```

### 步骤3：拉取并合并更新

**方式A：直接合并（推荐）**
```bash
git pull origin main --no-rebase
```

**方式B：变基合并（保持历史线性）**
```bash
git pull origin main --rebase
```

**方式C：先查看后决定**
```bash
# 只拉取不合并
git fetch origin main

# 手动查看变更
git diff HEAD..origin/main

# 确认后合并
git merge origin/main
```

---

## 3. 解决可能的冲突

如果出现冲突，Git会提示冲突文件。处理步骤：

```bash
# 查看冲突文件
git status

# 手动编辑冲突文件，解决冲突后：
git add <冲突文件>
git commit -m "合并Gitee超声更新"
```

---

## 4. 更新后再推送到GitHub

拉取Gitee更新并解决冲突后，同步到GitHub：

```bash
git push github main:PA1664WorkbenchTmp
```

---

## 本次提交内容

**提交哈希**：`03d862cc5`  
**提交信息**：修复触摸屏兼容性 - 方向按钮支持按住操作

**变更文件**：
- ✅ `src/jog_button_touch_filter.h` （新增）- 触摸事件过滤器
- ✅ `src/robotcontrolpanel.cpp` （修改）- 安装触摸过滤器
- ✅ `TOUCHSCREEN_FIX.md` （新增）- 修复方案文档
- ✅ `触摸屏问题诊断说明.txt` （新增）- 问题诊断记录

**未修改部分**：
- ❌ 机器人定位算法
- ❌ 焊道检测
- ❌ 纠偏算法
- ❌ 电机控制
- ❌ 速度计算

---

## 快速操作脚本

如果你希望一次性完成所有操作，可以执行：

```bash
cd D:\dev\deskCrawlingRobot\CrawlingRobotSV\PA1664WorkbenchTmp

# 1. 推送到GitHub
git push github main:PA1664WorkbenchTmp --force

# 2. 从Gitee拉取更新
git fetch origin main
git log HEAD..origin/main --oneline
git pull origin main --no-rebase

# 3. 再次推送到GitHub（包含Gitee更新）
git push github main:PA1664WorkbenchTmp
```

---

## 注意事项

1. **强制推送风险**：`--force` 会覆盖远程历史，确保其他人没有基于旧历史的工作
2. **冲突处理**：如果Gitee有超声相关更新，可能与你的本地修改冲突，需要手动解决
3. **网络环境**：自动化环境无法访问远程仓库，需要你在本地Windows系统执行
4. **认证要求**：Git操作需要你的Gitee和GitHub凭据

---

## 检查远程仓库状态

**GitHub仓库**：https://github.com/chuge007/rosCar/tree/PA1664WorkbenchTmp  
**Gitee仓库**：https://gitee.com/the-good-guy/pa1664-workbench/tree/main/

可以在浏览器中访问这些链接，手动检查更新内容。
