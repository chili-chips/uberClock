#!/usr/bin/env python3
"""
Compares the shipped trackq interpolator (Hann window + log-power 3-point
parabola, uberclock.c:trackq_fft_peak_vertex_estimate_mhz) against the
standard DFT-interpolation estimators from the literature already cited in
the investigation record's Sources section (Jacobsen & Kootsookos 2007,
Quinn 1994), plus Candan's closed-form bias correction of Jacobsen's
estimator (Candan, IEEE SPL 2011/2013).

Also re-runs the "fit a quadratic to Re(k) and a separate quadratic to
Im(k), then maximize the resulting 4th-order |.|^2 curve" idea raised in a
meeting discussion, to document numerically why it was not adopted.

No FFT library or numpy needed: for a 3-bin interpolator, only X(k-1), X(k),
X(k+1) are ever used, so each is computed directly as a length-N DFT sum
(cmath, standard library only). matplotlib is used only for the plots, the
same as this directory's analyze_sweep_capture.py / analyze_loopback_capture.py.

No hardware required -- this is a pure signal-model simulation: a clean
single tone at a known fractional bin offset, windowed and DFT'd exactly as
the firmware does it, so "bias" here is the estimator's own mathematical
bias (noiseless), not anything measured on the bench.
"""
import argparse
import cmath
import math

import matplotlib.pyplot as plt

# Matches the bench setup used throughout the investigation record
# (Section 5/7/12): N=2048, Fs=10 kHz baseband IF, nominal peak at bin 205.
N = 2048
FS_HZ = 10000.0
BIN_HZ = FS_HZ / N
K0 = 205


# --------------------------------------------------------------------------
# Signal model: a single real tone at (K0+delta) bins, direct 3-bin DFT.
# --------------------------------------------------------------------------

def hann(n_idx):
    return 0.5 - 0.5 * math.cos((2.0 * math.pi * n_idx) / (N - 1))


def make_samples(delta_bins, phase, window):
    f_hz = (K0 + delta_bins) * BIN_HZ
    w = hann if window == "hann" else (lambda n: 1.0)
    return [w(n) * math.cos(2.0 * math.pi * f_hz * n / FS_HZ + phase) for n in range(N)]


def dft_bin(samples, k):
    """X(k) = sum_n samples[n] * exp(-j 2 pi k n / N) -- direct sum, no FFT needed."""
    acc = 0j
    w = cmath.exp(-2j * math.pi * k / N)
    wn = 1.0 + 0j
    for s in samples:
        acc += s * wn
        wn *= w
    return acc


def three_bins(delta_bins, phase, window):
    samples = make_samples(delta_bins, phase, window)
    return dft_bin(samples, K0 - 1), dft_bin(samples, K0), dft_bin(samples, K0 + 1)


# --------------------------------------------------------------------------
# Estimators. Each takes the three complex bins and returns the estimated
# bin index (K0 + fractional correction).
# --------------------------------------------------------------------------

def est_linear_power_quad(xm1, x0, xp1):
    """Original/baseline: linear-power 3-point parabola (pre-fix firmware)."""
    y1, y2, y3 = abs(xm1) ** 2, abs(x0) ** 2, abs(xp1) ** 2
    den = y1 - 2.0 * y2 + y3
    return K0 + 0.5 * (y1 - y3) / den


def est_log_power_quad(xm1, x0, xp1):
    """Shipped method: log-power 3-point parabola (uberclock.c:1197-1220)."""
    y1, y2, y3 = abs(xm1) ** 2, abs(x0) ** 2, abs(xp1) ** 2
    ly1, ly2, ly3 = math.log(y1), math.log(y2), math.log(y3)
    den = ly1 - 2.0 * ly2 + ly3
    return K0 + 0.5 * (ly1 - ly3) / den


def est_jacobsen(xm1, x0, xp1):
    """Jacobsen's estimator (Jacobsen & Kootsookos 2007, eq. as republished):
    delta = Re[(X(k-1)-X(k+1)) / (2X(k)-X(k-1)-X(k+1))]."""
    num = xm1 - xp1
    den = 2.0 * x0 - xm1 - xp1
    return K0 + (num / den).real


def est_jacobsen_candan2011(xm1, x0, xp1):
    """Candan 2011 (IEEE SPL vol.18 no.6): exact closed-form bias correction
    of Jacobsen's estimator, derived specifically for a RECTANGULAR window:
        correction_factor = tan(pi/N) / (pi/N)
    Applying this constant to Hann-windowed bins is *not* derived/justified
    by Candan's paper and is included here only to show what happens if you
    try it anyway (see results)."""
    raw = est_jacobsen(xm1, x0, xp1) - K0
    c1 = math.tan(math.pi / N) / (math.pi / N)
    return K0 + c1 * raw


def est_jacobsen_candan2013(xm1, x0, xp1):
    """Candan's refined (arctan) correction, also rectangular-window-exact."""
    qc1 = est_jacobsen_candan2011(xm1, x0, xp1) - K0
    qc2 = math.atan(qc1 * math.pi / N) / (math.pi / N)
    return K0 + qc2


def est_quinn2(xm1, x0, xp1):
    """Quinn's second estimator (Quinn 1994): ratio-based, no window-specific
    correction constant needed."""
    a1 = (xm1 / x0).real
    a2 = (xp1 / x0).real
    d1 = a1 / (1.0 - a1)
    d2 = -a2 / (1.0 - a2)
    delta = d2 if (d1 > 0.0 and d2 > 0.0) else d1
    return K0 + delta


def est_quartic_reim(xm1, x0, xp1):
    """The 'fit a quadratic to Re(k), another to Im(k), maximize the
    resulting quartic |.|^2 curve' idea raised in discussion. 3 points fully
    determine each quadratic (exact interpolation, no free parameters), so
    this is evaluated with a fine grid search over the quartic rather than
    any closed form. Included to document why it was rejected, not as a
    candidate for adoption."""
    re = (xm1.real, x0.real, xp1.real)
    im = (xm1.imag, x0.imag, xp1.imag)

    def quad_coeffs(y):
        # Lagrange quadratic through (-1,y[0]), (0,y[1]), (1,y[2])
        a = (y[0] - 2.0 * y[1] + y[2]) / 2.0
        b = (y[2] - y[0]) / 2.0
        c = y[1]
        return a, b, c

    are_, bre_, cre_ = quad_coeffs(re)
    aim_, bim_, cim_ = quad_coeffs(im)
    best_x, best_p = 0.0, -1.0
    steps = 4001
    for i in range(steps):
        x = -1.0 + 2.0 * i / (steps - 1)
        rv = are_ * x * x + bre_ * x + cre_
        iv = aim_ * x * x + bim_ * x + cim_
        p = rv * rv + iv * iv
        if p > best_p:
            best_p, best_x = p, x
    return K0 + best_x


ESTIMATORS = {
    "linear-power quad (orig. firmware)": est_linear_power_quad,
    "log-power quad (SHIPPED)": est_log_power_quad,
    "Jacobsen": est_jacobsen,
    "Jacobsen+Candan 2011 (rect-exact)": est_jacobsen_candan2011,
    "Jacobsen+Candan 2013 (rect-exact)": est_jacobsen_candan2013,
    "Quinn 2nd estimator": est_quinn2,
    "quartic Re/Im fit (rejected)": est_quartic_reim,
}


def bias_curve(name, estimator, window, deltas, phase=0.0):
    out = []
    for d in deltas:
        xm1, x0, xp1 = three_bins(d, phase, window)
        est = estimator(xm1, x0, xp1)
        out.append(est - (K0 + d))
    return out


# --------------------------------------------------------------------------
# Two-tone interferer robustness sweep.
#
# Every bias number above assumes a single, perfectly isolated tone -- the
# idealization Jacobsen/Quinn/Candan's closed-form derivations depend on.
# uberclock.c:1147-1150 explicitly worries about a second case: "any
# unrelated, momentarily-stronger spectral component (image, harmonic,
# filter leakage)" showing up alongside the tracked tone. This section
# injects exactly that -- a second tone at a deliberately non-bin-aligned
# offset (so it genuinely leaks across bins rather than landing cleanly on
# one) -- and compares how much that leakage biases rect+Jacobsen (no
# window-derived sidelobe suppression) against the shipped Hann+log-power
# estimator, as a function of the interferer's distance and amplitude.
# --------------------------------------------------------------------------

def make_two_tone(delta_bins, interferer_offset_bins, interferer_amp_ratio, window, interferer_phase=0.37):
    f1 = (K0 + delta_bins) * BIN_HZ
    f2 = (K0 + interferer_offset_bins) * BIN_HZ
    w = hann if window == "hann" else (lambda n: 1.0)
    out = []
    for n in range(N):
        s = (math.cos(2.0 * math.pi * f1 * n / FS_HZ)
             + interferer_amp_ratio * math.cos(2.0 * math.pi * f2 * n / FS_HZ + interferer_phase))
        out.append(w(n) * s)
    return out


def interferer_bias(delta_bins, interferer_offset_bins, interferer_amp_ratio, window, estimator):
    samples = make_two_tone(delta_bins, interferer_offset_bins, interferer_amp_ratio, window)
    xm1 = dft_bin(samples, K0 - 1)
    x0 = dft_bin(samples, K0)
    xp1 = dft_bin(samples, K0 + 1)
    est = estimator(xm1, x0, xp1)
    return est - (K0 + delta_bins)


# (method name, window, estimator) pair compared throughout this sweep.
RECT_JACOBSEN = ("rect+Jacobsen", "rect", est_jacobsen)
HANN_SHIPPED = ("Hann+log-power (SHIPPED)", "hann", est_log_power_quad)

INTERFERER_DISTANCES_BINS = [30.5, 20.5, 15.5, 10.5, 8.5, 6.5, 5.5, 4.5, 3.5, 2.5, 2.0, 1.75, 1.5, 1.25, 1.1]
INTERFERER_AMPLITUDE_DB = [-40, -30, -20, -14, -10, -6, 0, 6]


def run_interferer_sweep():
    print("=== Two-tone interferer robustness sweep (N={}) ===\n".format(N))
    print("Primary tone bin-centered (delta=0.0); interferer deliberately placed")
    print("off-bin (k0 + integer + 0.5) so it genuinely leaks across bins instead")
    print("of landing cleanly on one.\n")

    print("--- Fixed 0 dB (equal-amplitude) interferer, swept by distance ---")
    header = f"{'interferer dist (bins)':>24}{RECT_JACOBSEN[0]:>22}{HANN_SHIPPED[0]:>28}{'rect worse by':>16}"
    print(header)
    dist_results = {RECT_JACOBSEN[0]: [], HANN_SHIPPED[0]: []}
    for off in INTERFERER_DISTANCES_BINS:
        rb = interferer_bias(0.0, off, 1.0, RECT_JACOBSEN[1], RECT_JACOBSEN[2])
        hb = interferer_bias(0.0, off, 1.0, HANN_SHIPPED[1], HANN_SHIPPED[2])
        dist_results[RECT_JACOBSEN[0]].append(rb)
        dist_results[HANN_SHIPPED[0]].append(hb)
        ratio = abs(rb) / abs(hb) if hb != 0 else float("inf")
        print(f"{off:24.2f}{rb:22.5f}{hb:28.5f}{ratio:15.1f}x")
    print()

    print("--- Fixed 8.5-bin interferer, swept by amplitude relative to the tracked tone ---")
    header = f"{'amplitude (dB)':>18}{RECT_JACOBSEN[0]:>22}{HANN_SHIPPED[0]:>28}"
    print(header)
    amp_results = {RECT_JACOBSEN[0]: [], HANN_SHIPPED[0]: []}
    for db in INTERFERER_AMPLITUDE_DB:
        ratio_lin = 10.0 ** (db / 20.0)
        rb = interferer_bias(0.0, 8.5, ratio_lin, RECT_JACOBSEN[1], RECT_JACOBSEN[2])
        hb = interferer_bias(0.0, 8.5, ratio_lin, HANN_SHIPPED[1], HANN_SHIPPED[2])
        amp_results[RECT_JACOBSEN[0]].append(rb)
        amp_results[HANN_SHIPPED[0]].append(hb)
        print(f"{db:17d} {rb:22.5f}{hb:28.5f}")
    print()

    return dist_results, amp_results


def plot_interferer_sweep(dist_results, amp_results, save_prefix):
    fig, ax = plt.subplots(figsize=(9, 6))
    for label, vals in dist_results.items():
        ax.plot(INTERFERER_DISTANCES_BINS, vals, marker="o", markersize=4, label=label)
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.invert_xaxis()
    ax.set_xlabel("interferer distance from tracked tone (bins)")
    ax.set_ylabel("estimator bias (bins)")
    ax.set_title("Bias vs. interferer distance, equal amplitude (0 dB)")
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(f"{save_prefix}_interferer_distance.png", dpi=150)
    print(f"Saved {save_prefix}_interferer_distance.png")

    fig, ax = plt.subplots(figsize=(9, 6))
    for label, vals in amp_results.items():
        ax.plot(INTERFERER_AMPLITUDE_DB, vals, marker="o", markersize=4, label=label)
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.set_xlabel("interferer amplitude relative to tracked tone (dB)")
    ax.set_ylabel("estimator bias (bins)")
    ax.set_title("Bias vs. interferer amplitude, fixed 8.5-bin distance")
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(f"{save_prefix}_interferer_amplitude.png", dpi=150)
    print(f"Saved {save_prefix}_interferer_amplitude.png")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--save", default="fft_interp_estimator",
                     help="output PNG filename prefix (default: fft_interp_estimator)")
    args = ap.parse_args()

    deltas = [i * 0.025 for i in range(21)]  # 0.000 .. 0.500 bin, step 0.025

    print(f"=== FFT interpolation estimator comparison, N={N}, Fs={FS_HZ:.0f} Hz, "
          f"bin width={BIN_HZ:.4f} Hz ===\n")
    print("Noiseless single-tone simulation: bias = estimated bin - true bin,")
    print("swept across one full FFT bin (delta = 0 is bin-centered, delta = 0.5")
    print("is exactly on the boundary between two bins).\n")

    results = {}  # (name, window) -> list of bias values aligned with deltas
    combos = [
        ("linear-power quad (orig. firmware)", "rect"),
        ("log-power quad (SHIPPED)", "rect"),
        ("Jacobsen", "rect"),
        ("Jacobsen+Candan 2011 (rect-exact)", "rect"),
        ("Jacobsen+Candan 2013 (rect-exact)", "rect"),
        ("log-power quad (SHIPPED)", "hann"),
        ("Jacobsen", "hann"),
        ("Jacobsen+Candan 2011 (rect-exact)", "hann"),
        ("Quinn 2nd estimator", "hann"),
        ("quartic Re/Im fit (rejected)", "hann"),
    ]
    for name, window in combos:
        results[(name, window)] = bias_curve(name, ESTIMATORS[name], window, deltas)

    def fmt_row(label, vals):
        cells = "".join(f"{v:9.4f}" for v in vals)
        return f"{label:38s}{cells}"

    header = "".join(f"{d:9.3f}" for d in deltas)
    print("--- delta (bins) ---")
    print(f"{'estimator (window)':38s}{header}")
    for (name, window), vals in results.items():
        print(fmt_row(f"{name} [{window}]", vals))
    print()

    print("--- peak |bias| in bins, and in Hz at this N/Fs ---")
    for (name, window), vals in results.items():
        peak = max(abs(v) for v in vals)
        print(f"{name:38s}[{window:4s}]  {peak:8.5f} bin   {peak * BIN_HZ:8.4f} Hz")
    print()

    # --- Plot 1: overview, includes the broken quartic estimator so its
    # scale is visible relative to everything else.
    fig, ax = plt.subplots(figsize=(9, 6))
    for (name, window), vals in results.items():
        if window != "hann":
            continue
        ax.plot(deltas, vals, marker="o", markersize=3,
                label=f"{name} [hann]")
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.set_xlabel("true fractional bin offset (delta, bins)")
    ax.set_ylabel("estimator bias (bins)")
    ax.set_title(f"Hann-windowed bias, overview (N={N}) -- includes the rejected quartic fit")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(f"{args.save}_overview.png", dpi=150)
    print(f"Saved {args.save}_overview.png")

    # --- Plot 2: zoomed, Hann-window estimators worth actually comparing
    # (excludes the quartic fit, which is ~40x larger in scale).
    fig, ax = plt.subplots(figsize=(9, 6))
    for (name, window) in combos:
        if window != "hann" or "quartic" in name:
            continue
        ax.plot(deltas, results[(name, window)], marker="o", markersize=3,
                label=f"{name}")
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.set_xlabel("true fractional bin offset (delta, bins)")
    ax.set_ylabel("estimator bias (bins)")
    ax.set_title(f"Hann-windowed bias, zoomed (N={N})")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(f"{args.save}_hann_zoomed.png", dpi=150)
    print(f"Saved {args.save}_hann_zoomed.png")

    # --- Plot 3: rectangular-window estimators (where Jacobsen/Candan are
    # actually derived to be exact).
    fig, ax = plt.subplots(figsize=(9, 6))
    for (name, window) in combos:
        if window != "rect":
            continue
        ax.plot(deltas, results[(name, window)], marker="o", markersize=3,
                label=f"{name}")
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.set_xlabel("true fractional bin offset (delta, bins)")
    ax.set_ylabel("estimator bias (bins)")
    ax.set_title(f"Rectangular-window bias (N={N}) -- Candan's native domain")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(f"{args.save}_rect.png", dpi=150)
    print(f"Saved {args.save}_rect.png")

    # --- Two-tone interferer robustness sweep ---
    dist_results, amp_results = run_interferer_sweep()
    plot_interferer_sweep(dist_results, amp_results, args.save)


if __name__ == "__main__":
    main()
