#!/usr/bin/env python3
"""
焊道点云分析和可视化工具

功能:
1. 加载PLY点云文件
2. 可视化点云和焊道定位结果
3. 对比不同算法的定位结果
4. 分析多帧连续性
"""

import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
import sys

class WeldProfileAnalyzer:
    def __init__(self):
        self.profiles = []
        self.results_old = []
        self.results_new = []

    def load_ply(self, filename):
        """加载PLY文件"""
        points = []
        with open(filename, 'r') as f:
            in_header = True
            vertex_count = 0

            for line in f:
                line = line.strip()

                if in_header:
                    if line.startswith('element vertex'):
                        vertex_count = int(line.split()[2])
                    if line == 'end_header':
                        in_header = False
                    continue

                if not line:
                    continue

                parts = line.split()
                if len(parts) >= 3:
                    try:
                        x, y, z = float(parts[0]), float(parts[1]), float(parts[2])
                        if np.isfinite(x) and np.isfinite(z) and z > 0:
                            points.append([x, y, z])
                    except ValueError:
                        continue

        return np.array(points)

    def estimate_baseline(self, x, z):
        """估计基准面 - 使用两端中位数"""
        n = len(x)
        edge_count = max(8, n // 10)

        left_x = x[:edge_count]
        left_z = z[:edge_count]
        right_x = x[-edge_count:]
        right_z = z[-edge_count:]

        lx, lz = np.median(left_x), np.median(left_z)
        rx, rz = np.median(right_x), np.median(right_z)

        slope = (rz - lz) / (rx - lx + 1e-9)
        offset = lz - slope * lx

        return slope, offset

    def compute_residuals(self, x, z, slope, offset):
        """计算高度残差"""
        baseline = slope * x + offset
        residuals = z - baseline
        return residuals

    def detect_weld_simple(self, x, z):
        """简单焊道检测 - 模拟现有算法"""
        # 估计基准面
        slope, offset = self.estimate_baseline(x, z)
        residuals = self.compute_residuals(x, z, slope, offset)

        # 计算噪声水平
        errors = np.abs(residuals)
        noise = 1.4826 * np.median(errors)

        # 阈值检测
        threshold = 2.5 * noise
        weld_mask = residuals > threshold

        if not np.any(weld_mask):
            return None

        # 找到连续区域
        indices = np.where(weld_mask)[0]
        if len(indices) < 5:
            return None

        start_idx = indices[0]
        end_idx = indices[-1]

        # 加权质心 (线性权重)
        weights = residuals[start_idx:end_idx+1] - threshold
        weights = np.maximum(weights, 0)

        if np.sum(weights) < 1e-9:
            center_x = (x[start_idx] + x[end_idx]) / 2
        else:
            center_x = np.sum(x[start_idx:end_idx+1] * weights) / np.sum(weights)

        return {
            'center_x': center_x,
            'start_idx': start_idx,
            'end_idx': end_idx,
            'width': x[end_idx] - x[start_idx],
            'height': np.max(residuals[start_idx:end_idx+1]),
            'slope': slope,
            'offset': offset,
            'noise': noise,
            'method': 'simple_weighted_centroid'
        }

    def detect_weld_enhanced(self, x, z):
        """增强焊道检测 - 模拟新算法"""
        # 估计基准面
        slope, offset = self.estimate_baseline(x, z)
        residuals = self.compute_residuals(x, z, slope, offset)

        # 计算噪声水平
        errors = np.abs(residuals)
        noise = 1.4826 * np.median(errors)

        # 自适应阈值
        positive_residuals = residuals[residuals > 0]
        if len(positive_residuals) > 0:
            p75 = np.percentile(positive_residuals, 75)
            p95 = np.percentile(positive_residuals, 95)

            if p95 > 2.5 * noise:
                threshold = max(0.5, p75 * 0.4)  # 自适应
            else:
                threshold = 2.5 * noise
        else:
            threshold = 2.5 * noise

        # 阈值检测
        weld_mask = residuals > threshold

        if not np.any(weld_mask):
            return None

        # 找到连续区域
        indices = np.where(weld_mask)[0]
        if len(indices) < 5:
            return None

        start_idx = indices[0]
        end_idx = indices[-1]

        # 扩展到肩部
        shoulder_expansion = int((end_idx - start_idx) * 0.1)
        for i in range(1, shoulder_expansion + 1):
            if start_idx - i >= 0 and residuals[start_idx - i] > 0:
                start_idx = start_idx - i
            else:
                break
        for i in range(1, shoulder_expansion + 1):
            if end_idx + i < len(residuals) and residuals[end_idx + i] > 0:
                end_idx = end_idx + i
            else:
                break

        # 鲁棒中心估计 (二次权重)
        max_height = np.max(residuals[start_idx:end_idx+1])
        floor = max(threshold, max_height * 0.3)

        heights = residuals[start_idx:end_idx+1]
        weights = np.maximum(heights - floor, 0) ** 2  # 二次权重

        if np.sum(weights) < 1e-9:
            peak_idx = start_idx + np.argmax(residuals[start_idx:end_idx+1])
            center_x = x[peak_idx]
        else:
            center_x = np.sum(x[start_idx:end_idx+1] * weights) / np.sum(weights)

        return {
            'center_x': center_x,
            'start_idx': start_idx,
            'end_idx': end_idx,
            'width': x[end_idx] - x[start_idx],
            'height': max_height,
            'slope': slope,
            'offset': offset,
            'noise': noise,
            'threshold': threshold,
            'method': 'enhanced_quadratic_weighted'
        }

    def visualize_profile(self, filename, show_comparison=True):
        """可视化单个点云轮廓"""
        print(f"\n分析文件: {Path(filename).name}")

        points = self.load_ply(filename)
        if len(points) == 0:
            print("  加载失败")
            return

        x, y, z = points[:, 0], points[:, 1], points[:, 2]
        print(f"  有效点数: {len(x)}")
        print(f"  X范围: [{x.min():.1f}, {x.max():.1f}] 宽度={x.max()-x.min():.1f}mm")
        print(f"  Z范围: [{z.min():.1f}, {z.max():.1f}] 高度变化={z.max()-z.min():.1f}mm")

        # 检测焊道
        result_old = self.detect_weld_simple(x, z)
        result_new = self.detect_weld_enhanced(x, z)

        if result_old is None and result_new is None:
            print("  ❌ 两种算法都未检测到焊道")
            return

        # 可视化
        fig, axes = plt.subplots(2, 1, figsize=(14, 10))

        # 子图1: 原始轮廓
        ax = axes[0]
        ax.plot(x, z, 'b.', markersize=2, alpha=0.5, label='原始点云')

        if result_old:
            baseline = result_old['slope'] * x + result_old['offset']
            ax.plot(x, baseline, 'g--', linewidth=1, label='基准面')

            start_idx = result_old['start_idx']
            end_idx = result_old['end_idx']
            ax.plot(x[start_idx:end_idx+1], z[start_idx:end_idx+1],
                   'r.', markersize=4, label='焊道区域(旧)')
            ax.axvline(result_old['center_x'], color='r', linestyle='--',
                      linewidth=2, label=f"中心(旧): {result_old['center_x']:.2f}mm")

        if result_new:
            start_idx = result_new['start_idx']
            end_idx = result_new['end_idx']
            ax.plot(x[start_idx:end_idx+1], z[start_idx:end_idx+1],
                   'm.', markersize=4, label='焊道区域(新)', alpha=0.7)
            ax.axvline(result_new['center_x'], color='m', linestyle='--',
                      linewidth=2, label=f"中心(新): {result_new['center_x']:.2f}mm")

        ax.set_xlabel('X (mm)', fontsize=12)
        ax.set_ylabel('Z (mm)', fontsize=12)
        ax.set_title('原始点云轮廓和焊道定位', fontsize=14, fontweight='bold')
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

        # 子图2: 高度残差
        ax = axes[1]

        if result_old:
            residuals_old = self.compute_residuals(x, z, result_old['slope'],
                                                   result_old['offset'])
            ax.plot(x, residuals_old, 'b.', markersize=2, alpha=0.5,
                   label='残差(基准面拟合)')
            ax.axhline(result_old['noise'] * 2.5, color='orange', linestyle=':',
                      linewidth=1, label=f"阈值(旧): {result_old['noise']*2.5:.2f}mm")

        if result_new:
            residuals_new = self.compute_residuals(x, z, result_new['slope'],
                                                   result_new['offset'])
            ax.axhline(result_new['threshold'], color='purple', linestyle=':',
                      linewidth=1, label=f"阈值(新): {result_new['threshold']:.2f}mm")

            # 标记焊道区域
            start_idx = result_new['start_idx']
            end_idx = result_new['end_idx']
            ax.fill_between(x[start_idx:end_idx+1], 0,
                           residuals_new[start_idx:end_idx+1],
                           alpha=0.3, color='red', label='焊道高度')

            ax.axvline(result_new['center_x'], color='m', linestyle='--',
                      linewidth=2, label=f"中心(新): {result_new['center_x']:.2f}mm")

        ax.axhline(0, color='k', linestyle='-', linewidth=0.5)
        ax.set_xlabel('X (mm)', fontsize=12)
        ax.set_ylabel('高度残差 (mm)', fontsize=12)
        ax.set_title('高度残差分析', fontsize=14, fontweight='bold')
        ax.legend(fontsize=10)
        ax.grid(True, alpha=0.3)

        plt.tight_layout()

        # 输出结果对比
        if result_old and result_new:
            print(f"\n  算法对比:")
            print(f"    中心位置差异: {abs(result_old['center_x'] - result_new['center_x']):.2f}mm")
            print(f"    旧算法: centerX={result_old['center_x']:.2f}mm, "
                  f"width={result_old['width']:.1f}mm, height={result_old['height']:.2f}mm")
            print(f"    新算法: centerX={result_new['center_x']:.2f}mm, "
                  f"width={result_new['width']:.1f}mm, height={result_new['height']:.2f}mm")
        elif result_old:
            print(f"  ✓ 旧算法: centerX={result_old['center_x']:.2f}mm")
            print(f"  ❌ 新算法: 未检测到")
        elif result_new:
            print(f"  ❌ 旧算法: 未检测到")
            print(f"  ✓ 新算法: centerX={result_new['center_x']:.2f}mm")

        return fig

    def analyze_continuity(self, filenames):
        """分析多帧连续性"""
        print(f"\n{'='*60}")
        print(f"连续性分析 - 共{len(filenames)}个文件")
        print(f"{'='*60}")

        centers_old = []
        centers_new = []
        heights_old = []
        heights_new = []

        for i, filename in enumerate(filenames):
            points = self.load_ply(filename)
            if len(points) == 0:
                continue

            x, z = points[:, 0], points[:, 2]

            result_old = self.detect_weld_simple(x, z)
            result_new = self.detect_weld_enhanced(x, z)

            if result_old:
                centers_old.append(result_old['center_x'])
                heights_old.append(result_old['height'])
            if result_new:
                centers_new.append(result_new['center_x'])
                heights_new.append(result_new['height'])

        # 统计分析
        if len(centers_old) >= 2:
            centers_old = np.array(centers_old)
            print(f"\n旧算法 ({len(centers_old)}帧):")
            print(f"  中心X平均值: {np.mean(centers_old):.2f}mm")
            print(f"  中心X标准差: {np.std(centers_old):.2f}mm")
            print(f"  最大跳变: {np.max(np.abs(np.diff(centers_old))):.2f}mm")
            print(f"  序列: {', '.join([f'{c:.2f}' for c in centers_old])}")

        if len(centers_new) >= 2:
            centers_new = np.array(centers_new)
            print(f"\n新算法 ({len(centers_new)}帧):")
            print(f"  中心X平均值: {np.mean(centers_new):.2f}mm")
            print(f"  中心X标准差: {np.std(centers_new):.2f}mm")
            print(f"  最大跳变: {np.max(np.abs(np.diff(centers_new))):.2f}mm")
            print(f"  序列: {', '.join([f'{c:.2f}' for c in centers_new])}")

        # 可视化
        fig, axes = plt.subplots(2, 1, figsize=(12, 8))

        # 子图1: 中心位置变化
        ax = axes[0]
        if len(centers_old) > 0:
            ax.plot(range(len(centers_old)), centers_old, 'ro-',
                   linewidth=2, markersize=8, label=f'旧算法 (std={np.std(centers_old):.2f}mm)')
        if len(centers_new) > 0:
            ax.plot(range(len(centers_new)), centers_new, 'bs-',
                   linewidth=2, markersize=8, label=f'新算法 (std={np.std(centers_new):.2f}mm)')
        ax.set_xlabel('帧序号', fontsize=12)
        ax.set_ylabel('中心X (mm)', fontsize=12)
        ax.set_title('焊道中心位置连续性', fontsize=14, fontweight='bold')
        ax.legend(fontsize=11)
        ax.grid(True, alpha=0.3)

        # 子图2: 帧间跳变
        ax = axes[1]
        if len(centers_old) > 1:
            jumps_old = np.abs(np.diff(centers_old))
            ax.plot(range(len(jumps_old)), jumps_old, 'ro-',
                   linewidth=2, markersize=8, label=f'旧算法 (max={np.max(jumps_old):.2f}mm)')
        if len(centers_new) > 1:
            jumps_new = np.abs(np.diff(centers_new))
            ax.plot(range(len(jumps_new)), jumps_new, 'bs-',
                   linewidth=2, markersize=8, label=f'新算法 (max={np.max(jumps_new):.2f}mm)')
        ax.axhline(0.5, color='g', linestyle='--', linewidth=1, label='目标: 0.5mm')
        ax.set_xlabel('帧序号', fontsize=12)
        ax.set_ylabel('帧间跳变 (mm)', fontsize=12)
        ax.set_title('帧间跳变分析', fontsize=14, fontweight='bold')
        ax.legend(fontsize=11)
        ax.grid(True, alpha=0.3)

        plt.tight_layout()

        return fig


def main():
    # 测试文件
    test_files = [
        r"C:\Users\a\AppData\Local\Claude-3p\local-agent-mode-sessions\8535f8b1\00000000\c494089d\uploads\bcaccc3b-b4d3-4c66-a0e9-12ffec9a7f0c-1792723306202_Image_Profile_20261023_09_16_36_972.ply",
        r"C:\Users\a\AppData\Local\Claude-3p\local-agent-mode-sessions\8535f8b1\00000000\c494089d\uploads\7f04f954-33a8-4e30-986a-a87872e9981b-1792723306203_Image_Profile_20261023_10_38_48_789.ply",
        r"C:\Users\a\AppData\Local\Claude-3p\local-agent-mode-sessions\8535f8b1\00000000\c494089d\uploads\0f4cc165-23f5-4e75-a18a-776613705870-1792723306203_Image_Profile_20261023_10_39_48_175.ply",
        r"C:\Users\a\AppData\Local\Claude-3p\local-agent-mode-sessions\8535f8b1\00000000\c494089d\uploads\2c85d691-5d49-4b60-b81c-f5b7ff6986db-1792723306203_Image_Profile_20261023_10_40_27_045.ply"
    ]

    analyzer = WeldProfileAnalyzer()

    # 分析每个文件
    for filename in test_files:
        if Path(filename).exists():
            fig = analyzer.visualize_profile(filename)
            if fig:
                output_name = f"profile_analysis_{Path(filename).stem}.png"
                fig.savefig(output_name, dpi=150, bbox_inches='tight')
                print(f"  保存图像: {output_name}")
                plt.close(fig)
        else:
            print(f"文件不存在: {filename}")

    # 连续性分析
    existing_files = [f for f in test_files if Path(f).exists()]
    if len(existing_files) > 1:
        fig = analyzer.analyze_continuity(existing_files)
        fig.savefig("continuity_analysis.png", dpi=150, bbox_inches='tight')
        print(f"\n保存连续性分析图: continuity_analysis.png")
        plt.close(fig)

    print(f"\n{'='*60}")
    print("分析完成!")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
