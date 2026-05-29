#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""STM32 植物记录仪上位机 - 入口。

运行: python run.py
依赖: pip install -r requirements.txt
"""
import sys

from PyQt5.QtWidgets import QApplication

from app.main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setOrganizationName("PlantRecorder")
    app.setApplicationName("植物记录仪上位机")
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
