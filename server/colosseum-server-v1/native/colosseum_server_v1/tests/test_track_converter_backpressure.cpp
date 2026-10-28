#include "server1/ports/ProcessPort.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QTimer>
#include <iostream>
using namespace server1::media;
void check(bool b, const char *s) {
  if (!b) {
    std::cerr << s << '\n';
    std::exit(1);
  }
}
void spin(int ms) {
  QEventLoop l;
  QTimer::singleShot(ms, &l, &QEventLoop::quit);
  l.exec();
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
#ifdef Q_OS_UNIX
  {
    ProcessPort port;
    quint64 id = 0;
    qsizetype received = 0, maximumChunk = 0;
    int finished = 0;
    bool pause = true;
    ProcessSpec spec("/usr/bin/ffmpeg",
                     {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                      "anoisesrc=sample_rate=48000", "-t", "60", "-f", "f32le",
                      "pipe:1"});
    spec.backpressuredOutput = true;
    ProcessCallbacks cb;
    cb.onStdout = [&](auto b) {
      received += b.size();
      maximumChunk = std::max(maximumChunk, b.size());
      if (pause) {
        check(port.pauseOutput(id), "pause accepted");
        pause = false;
      }
    };
    cb.onFinished = [&](auto r) {
      check(r.exitCode == 0, "ffmpeg exit 0");
      check(r.stdoutData.size() <= 65536, "retained output bound");
      ++finished;
    };
    id = port.start(spec, cb);
    spin(200);
    auto paused = received;
    check(paused > 0 && paused <= 65536, "bounded first chunk");
    spin(300);
    check(received == paused && !finished,
          "paused consumer leaves producer blocked");
    check(port.resumeOutput(id), "resume accepted");
    for (int i = 0; i < 50 && !finished; ++i)
      spin(20);
    check(finished == 1 && received == 60 * 48000 * 4, "resumed no lost bytes");
    check(maximumChunk <= 65536, "maximum delivery chunk bound");
    std::cout << "FFmpeg 60s PCM: " << received << " bytes, paused " << paused
              << ", max chunk " << maximumChunk << " PASS\n";
  }
  {
    ProcessPort port;
    quint64 id = 0;
    int finished = 0, closed = 0;
    ProcessSpec spec("/usr/bin/python3",
                     {"-c", "import os; os.write(1,b'x'*4096)"});
    spec.backpressuredOutput = true;
    ProcessCallbacks cb;
    cb.onStdout = [&](auto) { (void)port.pauseOutput(id); };
    cb.onOutputClosed = [&] { ++closed; };
    cb.onFinished = [&](auto) { ++finished; };
    id = port.start(spec, cb);
    spin(200);
    check(finished == 0 && closed == 0,
          "paused exited process retains real pipe EOF");
    (void)port.resumeOutput(id);
    spin(100);
    check(finished == 1 && closed == 1, "resume drains EOF before finish");
  }
  {
    auto *port = new ProcessPort;
    QPointer<ProcessPort> guard(port);
    ProcessSpec spec(
        "/usr/bin/python3",
        {"-c", "import os,time; os.write(1,b'x'*4096);time.sleep(10)"});
    spec.backpressuredOutput = true;
    ProcessCallbacks cb;
    cb.onStdout = [&](auto) { delete port; };
    (void)port->start(spec, cb);
    spin(200);
    check(!guard, "delete owner inside output callback");
  }
  {
    ProcessPort port;
    quint64 id = 0;
    int count = 0;
    ProcessSpec spec(
        "/usr/bin/python3",
        {"-c", "import os,time; os.write(1,b'x'*4096);time.sleep(10)"});
    spec.backpressuredOutput = true;
    ProcessCallbacks cb;
    cb.onStdout = [&](auto) { (void)port.pauseOutput(id); };
    cb.onFinished = [&](auto r) {
      check(r.canceled, "cancellation retained");
      ++count;
    };
    id = port.start(spec, cb);
    spin(100);
    check(port.cancel(id), "cancel paused process");
    spin(100);
    check(count == 1 && port.activeCount() == 0,
          "cancel exactly once and no child");
  }
  {
    ProcessPort port;
    ProcessDriver driver(port);
    quint64 id = 0;
    int count = 0;
    DriverSpec spec;
    spec.process = ProcessSpec("/usr/bin/python3",
                               {"-c", "import os;os.write(1,b'x'*4096)"});
    spec.process.backpressuredOutput = true;
    ProcessCallbacks cb;
    cb.onStdout = [&](auto) { (void)driver.pauseOutput(id); };
    cb.onFinished = [&](auto r) {
      check(r.canceled, "driver canceled exited paused stream");
      ++count;
    };
    id = driver.start(spec, cb);
    spin(100);
    check(driver.cancel(id), "driver cancel exited paused stream");
    check(count == 1,
          "driver synchronous completion does not invalidate iterator");
  }
  {
    ProcessPort port;
    quint64 id = 0;
    int pid = 0;
    bool done = false;
    ProcessSpec spec(
        "/usr/bin/python3",
        {"-c", "import subprocess,os,time; p=subprocess.Popen(['sleep','60']); "
               "os.write(1,str(p.pid).encode()); time.sleep(60)"});
    spec.backpressuredOutput = true;
    ProcessCallbacks cb;
    cb.onStdout = [&](auto b) {
      pid = b.toInt();
      (void)port.pauseOutput(id);
    };
    cb.onFinished = [&](auto) { done = true; };
    id = port.start(spec, cb);
    spin(100);
    check(pid > 0, "bounded child group fixture");
    check(port.cancel(id), "kill bounded owned group");
    spin(100);
    check(done, "bounded group parent exited");
    QFile stat(QString("/proc/%1/stat").arg(pid));
    if (stat.open(QIODevice::ReadOnly)) {
      auto state = stat.readAll();
      check(state.contains(") Z") || state.contains(") X"),
            "bounded group descendant not running");
    }
  }
  std::cout << "Owned pipe lifecycle PASS\n";
#else
  std::cout << "Owned pipe lane unsupported on this platform\n";
#endif
}
