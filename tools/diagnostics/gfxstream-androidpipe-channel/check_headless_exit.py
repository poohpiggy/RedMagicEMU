#!/usr/bin/env python3
"""Run the fixed gfxstream PostCmd::Exit case with and without a post worker."""
from pathlib import Path
import subprocess
import sys
import tempfile


def check_headless_exit(source, cxx):
    body = source.read_text().split("        case PostCmd::Exit:\n", 1)[1].split("        default:", 1)[0]
    with tempfile.TemporaryDirectory(prefix="rme-headless-exit-") as directory:
        test = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        test.write_text("""
enum class WorkerProcessingResult { Stop };
struct Worker {
    int exits = 0;
    void exit() { ++exits; }
};
WorkerProcessingResult shutdown(Worker* m_postWorker) {
""" + body + """
}
int main() {
    Worker worker;
    if (shutdown(&worker) != WorkerProcessingResult::Stop || worker.exits != 1) return 1;
    return shutdown(nullptr) == WorkerProcessingResult::Stop ? 0 : 2;
}
""")
        subprocess.run([cxx, "-std=c++17", "-fno-sanitize-recover=null", "-fsanitize=null",
                        test, "-o", binary], check=True, timeout=30)
        return subprocess.run([binary], timeout=5).returncode


if __name__ == "__main__":
    raise SystemExit(check_headless_exit(Path(sys.argv[1]), sys.argv[2]))
