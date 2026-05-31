# -*- coding: utf-8 -*-
"""差分模式互斥逻辑测试脚本

测试场景：
1. 通道0设为差分 → 通道1应被禁用
2. 通道1设为差分 → 通道0应被禁用
3. 通道0和通道1都设为单端 → 两者都应启用
4. 验证正负输入的自动设置
"""

import sys
from PyQt5.QtWidgets import QApplication
from app.main_window import MainWindow


def test_differential_logic():
    """测试差分模式的互斥逻辑"""
    print("=" * 60)
    print("差分模式互斥逻辑测试")
    print("=" * 60)

    app = QApplication(sys.argv)
    window = MainWindow()
    config_tab = window.tabs["config"]

    print("\n测试场景1: 通道0设为差分")
    print("-" * 60)
    # 设置通道0为差分模式
    config_tab.ch_widgets[0]["mode"].setCurrentIndex(1)

    # 检查通道0的负输入是否自动设为AIN1
    neg_input = config_tab.ch_widgets[0]["neg"].currentIndex()
    print(f"通道0负输入: AIN{neg_input} (期望: AIN1)")
    assert neg_input == 1, "通道0差分模式下负输入应为AIN1"

    # 检查通道1是否被禁用
    ch1_enabled = config_tab.ch_widgets[1]["mode"].isEnabled()
    print(f"通道1是否可编辑: {ch1_enabled} (期望: False)")
    assert not ch1_enabled, "通道0差分时，通道1应被禁用"
    print("✓ 测试通过")

    print("\n测试场景2: 通道1设为差分（先恢复通道0为单端）")
    print("-" * 60)
    # 先恢复通道0为单端
    config_tab.ch_widgets[0]["mode"].setCurrentIndex(0)

    # 设置通道1为差分模式
    config_tab.ch_widgets[1]["mode"].setCurrentIndex(1)

    # 检查通道1的负输入是否自动设为AIN0
    neg_input = config_tab.ch_widgets[1]["neg"].currentIndex()
    print(f"通道1负输入: AIN{neg_input} (期望: AIN0)")
    assert neg_input == 0, "通道1差分模式下负输入应为AIN0"

    # 检查通道0是否被禁用
    ch0_enabled = config_tab.ch_widgets[0]["mode"].isEnabled()
    print(f"通道0是否可编辑: {ch0_enabled} (期望: False)")
    assert not ch0_enabled, "通道1差分时，通道0应被禁用"
    print("✓ 测试通过")

    print("\n测试场景3: 通道0和通道1都设为单端")
    print("-" * 60)
    # 恢复通道1为单端
    config_tab.ch_widgets[1]["mode"].setCurrentIndex(0)

    # 检查两个通道是否都启用
    ch0_enabled = config_tab.ch_widgets[0]["mode"].isEnabled()
    ch1_enabled = config_tab.ch_widgets[1]["mode"].isEnabled()
    print(f"通道0是否可编辑: {ch0_enabled} (期望: True)")
    print(f"通道1是否可编辑: {ch1_enabled} (期望: True)")
    assert ch0_enabled, "两个通道都是单端时，通道0应启用"
    assert ch1_enabled, "两个通道都是单端时，通道1应启用"

    # 检查负输入是否都为AVSS
    ch0_neg = config_tab.ch_widgets[0]["neg"].currentIndex()
    ch1_neg = config_tab.ch_widgets[1]["neg"].currentIndex()
    print(f"通道0负输入: 索引{ch0_neg} (期望: 16=AVSS)")
    print(f"通道1负输入: 索引{ch1_neg} (期望: 16=AVSS)")
    assert ch0_neg == 16, "单端模式下负输入应为AVSS"
    assert ch1_neg == 16, "单端模式下负输入应为AVSS"
    print("✓ 测试通过")

    print("\n测试场景4: 验证其他通道对（2-3, 4-5, ...）")
    print("-" * 60)
    for pair_start in [2, 4, 6, 8, 10, 12, 14]:
        # 设置偶数通道为差分
        config_tab.ch_widgets[pair_start]["mode"].setCurrentIndex(1)

        # 检查负输入
        neg_input = config_tab.ch_widgets[pair_start]["neg"].currentIndex()
        expected_neg = pair_start + 1
        assert neg_input == expected_neg, f"通道{pair_start}差分时负输入应为AIN{expected_neg}"

        # 检查配对通道是否被禁用
        pair_enabled = config_tab.ch_widgets[pair_start + 1]["mode"].isEnabled()
        assert not pair_enabled, f"通道{pair_start}差分时，通道{pair_start + 1}应被禁用"

        # 恢复为单端
        config_tab.ch_widgets[pair_start]["mode"].setCurrentIndex(0)

        print(f"✓ 通道对 {pair_start}-{pair_start + 1} 测试通过")

    print("\n测试场景5: 验证正输入固定为AINn")
    print("-" * 60)
    for ch in range(16):
        pos_input = config_tab.ch_widgets[ch]["pos"].currentIndex()
        pos_enabled = config_tab.ch_widgets[ch]["pos"].isEnabled()
        print(f"通道{ch}: 正输入=AIN{pos_input}, 可编辑={pos_enabled}")
        assert pos_input == ch, f"通道{ch}的正输入应固定为AIN{ch}"
        assert not pos_enabled, f"通道{ch}的正输入应禁止编辑"
    print("✓ 测试通过")

    print("\n" + "=" * 60)
    print("所有测试通过！✓")
    print("=" * 60)


if __name__ == "__main__":
    try:
        test_differential_logic()
    except AssertionError as e:
        print(f"\n✗ 测试失败: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ 发生错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
