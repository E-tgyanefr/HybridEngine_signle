# hybridengine.__main__ — CLI（--renderhash parity 校验 / --run-frames N）
import sys

GOLDEN_HASH = 0x4634E387E024BE90


def renderhash():
    from .engine import GameEngine
    with GameEngine() as engine:
        for _ in range(120):
            engine.run_frame(1.0 / 60.0)
        h = engine.render_hash
        print(f"PY_RENDERHASH 1280x800 = {h:016X}")
        ok = h == GOLDEN_HASH
        print("PARITY OK" if ok else f"PARITY DRIFT (golden={GOLDEN_HASH:016X})")
        return 0 if ok else 1


def run_frames(n: int) -> int:
    from .engine import GameEngine
    with GameEngine() as engine:
        engine.run(frames=n, dt=1.0 / 60.0)
        print(f"ran {n} frames (hash={engine.render_hash:016X})")
        return 0


def main(argv) -> int:
    mode = argv[0] if argv else "--renderhash"
    if mode == "--renderhash":
        return renderhash()
    if mode == "--run-frames":
        n = int(argv[1]) if len(argv) > 1 else 5
        return run_frames(n)
    print("usage: python -m hybridengine --renderhash | --run-frames N")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
