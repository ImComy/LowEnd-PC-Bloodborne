"""Qt Quick front end for the Windows Bloodborne launcher."""
from pathlib import Path
import sys

from PySide6.QtCore import QTimer, QUrl
from PySide6.QtGui import QIcon
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuickControls2 import QQuickStyle
from PySide6.QtWidgets import QApplication

from bbport_launcher_backend import LauncherBackend
import bbport_launcher_win as launcher_core
import bbport_startup

# Splash stays visible as a loading indicator until the game starts (fallback safety timeout 60s).
SPLASH_MS = 60000
# A direct launch waits this long first, so Shift or a click on the toast can open the launcher.
GRACE_S = 2


def run_ui(route=None):
    """ROUTE comes from bbport_startup.route(); without one the launcher opens on Home."""
    QQuickStyle.setStyle('Basic')
    app = QApplication(sys.argv)
    app.setApplicationName('Bloodborne')
    icon_path = (Path(sys.executable).resolve().parent / 'launcher' / 'assets' / 'hunter-icon.ico') if getattr(sys, 'frozen', False) else (Path(__file__).resolve().parent / 'assets' / 'hunter-icon.ico')
    app.setWindowIcon(QIcon(str(icon_path)))
    backend = LauncherBackend(route)
    backend.connect_workers()
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty('launcher', backend)
    qml = Path(__file__).resolve().parent / 'qml'
    if getattr(sys, 'frozen', False):
        import bbport_launcher_win as core
        qml = core.PORT_DIR / 'launcher' / 'qml'

    def load(name):
        loaded = len(engine.rootObjects())
        engine.load(QUrl.fromLocalFile(str(qml / name)))
        if len(engine.rootObjects()) == loaded:
            raise RuntimeError(f'Could not load Qt Quick launcher: {qml / name}')
        return engine.rootObjects()[-1]

    launcher_window = []

    def open_launcher(page=None, step=0):
        if launcher_window:
            return
        if page:
            backend.open_at(page, step)
        backend.start_background_checks()
        window = load('Main.qml')
        launcher_window.append(window)
        backend.closeRequested.connect(window.close)
        app.aboutToQuit.connect(backend.shutdown_coop)
        app.setQuitOnLastWindowClosed(True)

    if not route or route['mode'] != 'direct':
        open_launcher()
        return app.exec()

    # Direct launch: no launcher window, only a toast while the game starts. Shift/Ctrl or a
    # click on the toast opens the launcher; so does a game that fails before it is running.
    app.setQuitOnLastWindowClosed(False)
    splash = load('Splash.qml')
    keys = QTimer()
    keys.setInterval(100)

    def close_splash():
        keys.stop()
        countdown.stop()
        splash.close()

    def show_launcher(page='Home'):
        close_splash()
        open_launcher(page)

    def check_keys():
        if bbport_startup.launcher_keys_held():
            show_launcher()

    def finished(code, started):
        close_splash()
        if launcher_window:
            return  # the launcher is open; the user decides when to quit
        if started or code == 0:
            app.quit()
        else:
            open_launcher('Log')

    def start():
        if launcher_window:
            return  # the player chose the launcher during the countdown
        close_timer.start()
        backend.play()
        if not backend.running:  # refused or could not start; the notice says why
            show_launcher()

    keys.timeout.connect(check_keys)
    # The update check waits until the game is running and only remembers what it finds.
    backend.gameStarted.connect(lambda: launcher_core.start_quiet_update_check(backend.app))
    splash.openLauncher.connect(show_launcher)
    backend.gameStarted.connect(close_splash)
    backend.gameFinished.connect(finished)
    keys.start()
    close_timer = QTimer()
    close_timer.setSingleShot(True)
    close_timer.setInterval(SPLASH_MS)
    close_timer.timeout.connect(close_splash)
    remaining = [GRACE_S]
    countdown = QTimer()
    countdown.setInterval(1000)

    def tick():
        remaining[0] -= 1
        splash.setProperty('countdown', remaining[0])
        if remaining[0] <= 0:
            countdown.stop()
            start()

    countdown.timeout.connect(tick)
    splash.setProperty('countdown', GRACE_S)
    if GRACE_S > 0:
        countdown.start()
    else:
        QTimer.singleShot(0, start)
    return app.exec()

if __name__ == "__main__":
    sys.exit(run_ui())
