#!/usr/bin/env python3
"""Exercise the accessible footer cancel action against the real converter."""
import os
os.environ['QT_QPA_PLATFORM']='offscreen'
import argparse,tempfile,threading,time
from pathlib import Path
from PySide6.QtCore import QTimer,Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication,QFileDialog,QMessageBox
import app
parser=argparse.ArgumentParser();parser.add_argument('rom',type=Path);args=parser.parse_args()
original_convert=app.convert
started=threading.Event()
def slow_conversion(source,destination,notify,cancel):
    def progress(phase,done,total):
        notify(phase,done,total)
        if phase=='Creating .sm64ds' and not started.is_set():
            started.set()
            assert cancel.wait(10),'GUI did not request cancel'
    return original_convert(source,destination,progress,cancel)
app.convert=slow_conversion
with tempfile.TemporaryDirectory() as directory:
    destination=Path(directory)/'cancel.sm64ds';destination.write_bytes(b'original')
    QFileDialog.getSaveFileName=staticmethod(lambda *a,**kw:(str(destination),''))
    QMessageBox.question=staticmethod(lambda *a,**kw:QMessageBox.StandardButton.Yes)
    state={'began':False,'cancel':False,'done':False,'error':None};deadline=time.monotonic()+15
    original_exec=QApplication.exec
    def tick():
        try:
            application=QApplication.instance()
            window=next(w for w in application.topLevelWidgets() if w.windowTitle()=='SM64DS Packer')
            if not state['began']:
                window.begin_conversion(str(args.rom.resolve()));state['began']=True
            elif started.is_set() and not state['cancel']:
                assert window.footer_button.accessibleName()=='Cancel'
                QTest.mouseClick(window.footer_button,Qt.MouseButton.LeftButton)
                state['cancel']=True
            elif state['cancel'] and window.worker is None:
                assert window.was_cancelled and window.footer_button.accessibleName()=='Choose ROM'
                assert destination.read_bytes()==b'original'
                assert not list(destination.parent.glob('*.part'))
                state['done']=True;application.quit();return
            assert time.monotonic()<deadline
        except Exception as error:
            state['error']=repr(error);QApplication.instance().quit();return
        QTimer.singleShot(20,tick)
    def timed_exec(self):
        QTimer.singleShot(0,tick);return original_exec()
    QApplication.exec=timed_exec
    assert app.gui()==0 and state['done'] and state['error'] is None,state
print('PASS actual footer Cancel interrupts real converter; original output retained; temporary removed; Choose ROM restored')
