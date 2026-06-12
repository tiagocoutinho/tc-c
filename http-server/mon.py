#!/usr/bin/env python3

import threading
import subprocess
import time

from watchdog.events import FileSystemEventHandler
from watchdog.observers import Observer


class MyEventHandler(FileSystemEventHandler):
    state = "waiting"
    def on_any_event(self, event) -> None:
        if event.is_directory:
            return
        if not event.event_type == "modified":
            return
        if not event.src_path.endswith(".c"):
            return
        if self.state == "waiting":
            self.start_make_and_run()
        elif self.state == "running":
            self.httpd.terminate()
            self.start_make_and_run()

    def start_make_and_run(self):
        task = threading.Thread(target=self.make_and_run)
        self.state = "compiling"
        task.start()

    def make_and_run(self):
        clear_screen = '\033[2J\033[H'
        print(clear_screen);
        print("--- compiling... ---")
        if subprocess.call(["clang", "-Wall", "-ansi", "-std=c23", "-O2", "-o", "httpd", "httpd.c"]):
            self.state = "waiting"
            return
        self.state = "running"
        print("--- running... ---")
        self.httpd = subprocess.Popen(["./httpd"])
        self.httpd.wait()


event_handler = MyEventHandler()
event_handler.start_make_and_run()
observer = Observer()
observer.schedule(event_handler, ".", recursive=False)
observer.start()
try:

    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\rCtrl-c pressed. Bailing out")
finally:
    observer.stop()
    observer.join()

