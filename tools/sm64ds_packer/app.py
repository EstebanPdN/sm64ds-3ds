#!/usr/bin/env python3
"""Original DS pixel screen around a local cross-platform ROM data converter."""
from __future__ import annotations
import argparse
from pathlib import Path
import sys
import threading
import time
from core import Cancelled, convert


def gui():
    from PySide6.QtCore import Qt, QThread, Signal
    from PySide6.QtGui import QPainter, QIcon
    from PySide6.QtWidgets import QApplication, QFileDialog, QMessageBox, QWidget, QPushButton
    from pixel_ui import PixelArt

    class Worker(QThread):
        changed = Signal(str, int)
        succeeded = Signal(str)
        failed = Signal(str)
        cancelled = Signal()
        def __init__(self, source, output):
            super().__init__()
            self.source, self.output = source, output
            self.cancel = threading.Event()
            self.last_progress = 0
        def run(self):
            def notify(phase, done, total):
                now = time.monotonic()
                if now-self.last_progress >= .08 or done == total:
                    start, span = {'Checking ROM': (0,15), 'Creating .sm64ds': (15,60), 'Verifying .sm64ds': (75,25)}.get(phase,(0,100))
                    self.changed.emit(phase, start + int(done*span/max(1,total)))
                    self.last_progress = now
            try:
                self.succeeded.emit(convert(self.source,self.output,notify,self.cancel)['path'])
            except Cancelled:
                self.cancelled.emit()
            except Exception as error:
                self.failed.emit(str(error))

    class Window(QWidget):
        def __init__(self):
            super().__init__()
            self.setWindowTitle('SM64DS Packer')
            self.setFixedSize(640,480)
            self.setAcceptDrops(True)
            self.setWindowIcon(QIcon(str(Path(__file__).with_name('assets')/'icon.png')))
            self.art = PixelArt()
            self.worker = None
            self.closing = False
            self.percent = 0
            self.status_text = 'DROP ROM HERE'
            self.filename = ''
            self.ready = False
            self.was_cancelled = False
            self.footer_button = QPushButton('CHOOSE ROM', self)
            self.footer_button.setGeometry(160,408,320,56)
            self.footer_button.setAccessibleName('Choose ROM')
            self.footer_button.setCursor(Qt.CursorShape.PointingHandCursor)
            self.footer_button.setStyleSheet('QPushButton { border: none; background: transparent; color: transparent; }')
            self.footer_button.clicked.connect(self.footer_action)
        def paintEvent(self,event):
            image = self.art.render(self.status_text,self.percent,self.worker is not None,
                                    self.ready,self.filename,self.was_cancelled)
            painter = QPainter(self)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform,False)
            painter.drawImage(self.rect(),image)
            painter.end()
        def footer_action(self):
            if self.worker is None:
                self.pick()
            else:
                self.cancel()
        def keyPressEvent(self,event):
            if event.key() == Qt.Key.Key_Escape and self.worker is not None:
                self.cancel()
            elif event.key() in (Qt.Key.Key_Return,Qt.Key.Key_Enter,Qt.Key.Key_Space) and self.worker is None:
                self.pick()
            else:
                super().keyPressEvent(event)
        def dragEnterEvent(self,event):
            urls = event.mimeData().urls()
            if self.worker is None and len(urls)==1 and urls[0].isLocalFile() and Path(urls[0].toLocalFile()).suffix.lower()=='.nds':
                event.acceptProposedAction()
        def dropEvent(self,event):
            self.begin_conversion(event.mimeData().urls()[0].toLocalFile())
        def pick(self):
            source,_ = QFileDialog.getOpenFileName(self,'Choose your Super Mario 64 DS ROM','','Nintendo DS ROM (*.nds *.NDS);;All files (*)')
            if source:
                self.begin_conversion(source)
        def begin_conversion(self,source):
            if self.worker is not None:
                return
            output,_ = QFileDialog.getSaveFileName(self,'Save game data',str(Path(source).with_suffix('.sm64ds')),'SM64DS data (*.sm64ds)')
            if not output:
                return
            if Path(output).suffix.lower()!='.sm64ds':
                output += '.sm64ds'
            if Path(output).exists() and QMessageBox.question(self,'Replace file?','Replace the existing .sm64ds file?') != QMessageBox.StandardButton.Yes:
                return
            self.percent = 0
            self.status_text = 'CHECKING ROM'
            self.filename = Path(source).name
            self.ready = False
            self.was_cancelled = False
            self.worker = Worker(source,output)
            self.worker.changed.connect(self.progress_changed)
            self.worker.succeeded.connect(self.conversion_succeeded)
            self.worker.failed.connect(self.conversion_failed)
            self.worker.cancelled.connect(self.conversion_cancelled)
            self.worker.finished.connect(self.worker_finished)
            self.footer_button.setText('CANCEL')
            self.footer_button.setAccessibleName('Cancel')
            self.worker.start()
            self.update()
        def progress_changed(self,phase,percent):
            self.status_text = phase
            self.percent = percent
            self.update()
        def conversion_succeeded(self,output):
            self.percent = 100
            self.status_text = 'READY!'
            self.ready = True
            self.filename = Path(output).name
            self.update()
        def conversion_failed(self,message):
            self.status_text = 'ROM ERROR'
            self.update()
            QMessageBox.warning(self,'Conversion failed',message)
        def conversion_cancelled(self):
            self.status_text = 'DROP ROM HERE'
            self.percent = 0
            self.was_cancelled = True
            self.update()
        def cancel(self):
            if self.worker is not None:
                self.worker.cancel.set()
                self.status_text = 'CANCELLING'
                self.update()
        def worker_finished(self):
            previous = self.worker
            self.worker = None
            self.footer_button.setText('CHOOSE ROM')
            self.footer_button.setAccessibleName('Choose ROM')
            previous.deleteLater()
            self.update()
            if self.closing:
                self.close()
        def closeEvent(self,event):
            if self.worker is not None:
                self.closing = True
                self.cancel()
                event.ignore()
            else:
                event.accept()

    application = QApplication(sys.argv[:1])
    application.setApplicationName('SM64DS Packer')
    window = Window()
    window.show()
    return application.exec()


def main():
    parser = argparse.ArgumentParser(description='Convert your European SM64DS ROM into one .sm64ds archive.')
    parser.add_argument('--rom',type=Path)
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    if args.rom is not None or args.output is not None:
        if args.rom is None or args.output is None:
            parser.error('--rom and --output are required together')
        if args.output.exists():
            parser.error('CLI refuses an existing output; choose a new filename')
        import json
        print(json.dumps(convert(args.rom,args.output)))
        return 0
    return gui()


if __name__=='__main__':
    raise SystemExit(main())
