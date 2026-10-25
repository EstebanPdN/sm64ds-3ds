#!/usr/bin/env python3
"""Qt offscreen integration test: one dropped ROM, save picker, worker completion."""
import os
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
import argparse
from pathlib import Path
import tempfile
import time
from PySide6.QtCore import QMimeData, QPoint, QPointF, QTimer, QUrl, Qt
from PySide6.QtGui import QDragEnterEvent, QDropEvent
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QFileDialog, QLabel, QPushButton, QMessageBox
import app

parser = argparse.ArgumentParser()
parser.add_argument('rom', type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    destination = Path(directory) / 'GUI test.sm64ds'
    QFileDialog.getSaveFileName = staticmethod(lambda *a, **kw: (str(destination), 'SM64DS data (*.sm64ds)'))
    def fail_dialog(parent, title, message):
        state['error'] = title + ': ' + message
        QApplication.instance().quit()
    QMessageBox.warning = staticmethod(fail_dialog)
    chooser_calls=[]
    QFileDialog.getOpenFileName = staticmethod(lambda *a, **kw: (chooser_calls.append(True) or '', ''))
    original_exec = QApplication.exec
    state = {'started': False, 'finished': False, 'error': None}
    deadline = time.monotonic() + 20
    def tick():
        try:
            application = QApplication.instance()
            window = next(w for w in application.topLevelWidgets() if w.windowTitle() == 'SM64DS Packer')
            if not state['started']:
                state['started'] = True
                QTest.mouseClick(window,Qt.MouseButton.LeftButton,pos=QPoint(20,20))
                assert not chooser_calls, 'Click outside footer opened chooser'
                QTest.mouseClick(window.footer_button,Qt.MouseButton.LeftButton)
                assert len(chooser_calls)==1, 'Footer button did not open chooser'
                assert window.footer_button.accessibleName()=='Choose ROM'
                mime = QMimeData()
                mime.setUrls([QUrl.fromLocalFile(str(args.rom.resolve()))])
                enter = QDragEnterEvent(QPoint(100, 100), Qt.DropAction.CopyAction, mime, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier)
                QApplication.sendEvent(window, enter)
                assert enter.isAccepted(), 'ROM drag was not accepted'
                drop = QDropEvent(QPointF(100, 100), Qt.DropAction.CopyAction, mime, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier)
                QApplication.sendEvent(window, drop)
                assert window.worker is not None, 'Drop did not start conversion'
            if state['started'] and window.worker is None:
                assert destination.stat().st_size == 15686873
                assert window.percent == 100
                assert window.ready and window.status_text == 'READY!'
                state['finished'] = True
                application.quit()
                return
            assert time.monotonic() < deadline, 'Worker exceeded deadline'
        except Exception as error:
            state['error'] = repr(error)
            QApplication.instance().quit()
            return
        QTimer.singleShot(20, tick)
    def timed_exec(self):
        QTimer.singleShot(0, tick)
        return original_exec()
    QApplication.exec = timed_exec
    result = app.gui()
    assert result == 0 and state['finished'] and state['error'] is None, state
    print('PASS: outside click ignored; actual accessible footer opens chooser; drop accepted, save destination selected, background conversion finished, progress100%, controls restored')
