import argparse
import logging
import random
import socket
import sys
from dataclasses import dataclass

DEFAULT_FEC_N = 100
DEFAULT_FEC_K = 70

MAX_DATAGRAM_SIZE = 65535

DEFAULT_BURST_CORRELATION = 0.6


@dataclass
class Stats:
    received: int = 0
    dropped: int = 0
    forwarded: int = 0

    @property
    def effective_drop_pct(self) -> float:
        return (self.dropped / self.received * 100.0) if self.received else 0.0

# simulate packet drops with configurable randomness
class ChaosDropper:
    def __init__(self, drop_rate: float, burst_mode: bool, correlation: float):
        self.drop_rate = drop_rate
        self.burst_mode = burst_mode
        self.correlation = correlation
        self._last_dropped = False

    # determine whether to drop the next packet based on the configured drop rate and burst mode
    def should_drop(self) -> bool:
        if self.burst_mode and random.random() < self.correlation:
            decision = self._last_dropped
        else:
            decision = random.random() < self.drop_rate
        self._last_dropped = decision
        return decision


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Layer-4 UDP chaos proxy: forwards or drops raw datagrams to test FEC recovery.",
    )
    parser.add_argument("--listen-port", type=int, default=5004,
                         help="UDP port to listen on (default: 5004)")
    parser.add_argument("--dest-ip", type=str, default="127.0.0.1",
                         help="Destination IP for surviving packets (default: 127.0.0.1)")
    parser.add_argument("--dest-port", type=int, default=5005,
                         help="Destination UDP port (default: 5005)")
    parser.add_argument("--drop-rate", type=float, default=0.0,
                         help="Average fraction of packets to drop, 0.0-1.0 (default: 0.0)")
    parser.add_argument("--burst-mode", action="store_true",
                         help="Enable correlated/bursty drops instead of independent random drops")
    parser.add_argument("--burst-correlation", type=float, default=DEFAULT_BURST_CORRELATION,
                         help=f"Burst-mode correlation strength, 0.0-1.0 (default: {DEFAULT_BURST_CORRELATION})")
    parser.add_argument("--stats-interval", type=int, default=200,
                         help="Print running stats every N received packets (default: 200)")
    parser.add_argument("--fec-n", type=int, default=DEFAULT_FEC_N,
                         help=f"FEC data-packet count N, for the startup risk warning only (default: {DEFAULT_FEC_N})")
    parser.add_argument("--fec-k", type=int, default=DEFAULT_FEC_K,
                         help=f"FEC parity-packet count K, for the startup risk warning only (default: {DEFAULT_FEC_K})")
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if not (0.0 <= args.drop_rate <= 1.0):
        sys.exit(f"--drop-rate must be between 0.0 and 1.0 (got {args.drop_rate})")
    if not (0.0 <= args.burst_correlation <= 1.0):
        sys.exit(f"--burst-correlation must be between 0.0 and 1.0 (got {args.burst_correlation})")
    if not (1 <= args.listen_port <= 65535) or not (1 <= args.dest_port <= 65535):
        sys.exit("port numbers must be between 1 and 65535")
    if args.stats_interval < 1:
        sys.exit("--stats-interval must be >= 1")
    if args.fec_n <= 0 or args.fec_k < 0:
        sys.exit("--fec-n must be > 0 and --fec-k must be >= 0")


def warn_if_exceeds_fec_capacity(args: argparse.Namespace, log: logging.Logger) -> None:
    threshold = args.fec_k / (args.fec_n + args.fec_k)
    if args.drop_rate > threshold:
        log.warning(
            "drop-rate %.2f%% exceeds the FEC recovery threshold (~%.2f%%, K=%d of N+K=%d) "
            "— blocks will likely fail to reconstruct at this loss level.",
            args.drop_rate * 100, threshold * 100, args.fec_k, args.fec_n + args.fec_k,
        )


def make_socket(listen_port: int, log: logging.Logger) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    except OSError:
        log.warning("could not raise SO_RCVBUF; high packet rates may see kernel-level drops")
    try:
        sock.bind(("0.0.0.0", listen_port))
    except OSError as e:
        sys.exit(f"failed to bind UDP :{listen_port}: {e}")
    return sock


def run(args: argparse.Namespace, log: logging.Logger) -> None:
    validate_args(args)
    warn_if_exceeds_fec_capacity(args, log)

    sock = make_socket(args.listen_port, log)
    dest = (args.dest_ip, args.dest_port)
    dropper = ChaosDropper(args.drop_rate, args.burst_mode, args.burst_correlation)
    stats = Stats()

    log.info(
        "chaos_router up: :%d -> %s:%d | drop_rate=%.1f%% burst_mode=%s (correlation=%.2f)",
        args.listen_port, args.dest_ip, args.dest_port,
        args.drop_rate * 100, args.burst_mode, args.burst_correlation,
    )

    try:
        while True:
            data, _addr = sock.recvfrom(MAX_DATAGRAM_SIZE)
            stats.received += 1

            if dropper.should_drop():
                stats.dropped += 1
            else:
                try:
                    sock.sendto(data, dest)
                    stats.forwarded += 1
                except OSError as e:
                    log.error("sendto %s:%d failed: %s", args.dest_ip, args.dest_port, e)

            if stats.received % args.stats_interval == 0:
                log.info(
                    "Received=%d Dropped=%d Forwarded=%d EffectiveDropRate=%.2f%%",
                    stats.received, stats.dropped, stats.forwarded, stats.effective_drop_pct,
                )
    except KeyboardInterrupt:
        pass
    finally:
        log.info(
            "shutting down. final totals — Received=%d Dropped=%d Forwarded=%d EffectiveDropRate=%.2f%%",
            stats.received, stats.dropped, stats.forwarded, stats.effective_drop_pct,
        )
        sock.close()


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
    log = logging.getLogger("chaos_router")
    args = build_arg_parser().parse_args()
    run(args, log)


if __name__ == "__main__":
    main()