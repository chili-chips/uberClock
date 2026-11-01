<!--
SPDX-FileCopyrightText: 2026 Ahmed Imamović
SPDX-FileCopyrightText: 2026 Tarik Hamedović
SPDX-License-Identifier: CC-BY-SA-4.0
-->

FFT Peak Interpolation
=======================

A tone almost never lands exactly on an FFT bin center. The tracking firmware
(see :doc:`/firmware/tracking` and :doc:`/examples/tracking_algorithm`) has to
turn three nearby bin values into a sub-bin frequency estimate, and the
accuracy of that estimate limits how well ``trackq`` can hold lock.

This page summarizes a short investigation into which three-point
interpolator to use for that estimate. The full write-up, with derivations
and additional plots, is available as a PDF:
:download:`FFT_Peak_Interpolation.pdf <../../../DSP/FFT_Peak_Interpolation.pdf>`.

Problem statement
------------------

Write the true tone frequency as :math:`k_0 + \delta` bins, with
:math:`\delta \in [-0.5, 0.5]` the fractional offset from the nearest bin
:math:`k_0`. Given only the three complex DFT values around the peak,

.. math::

   X_{-1} = X(k_0-1), \qquad X_0 = X(k_0), \qquad X_{+1} = X(k_0+1),

the interpolator's job is to estimate :math:`\delta` as accurately as
possible.

Two ways to fit three points
-----------------------------

**Fit the complex spectrum (quartic).** Fitting an independent quadratic to
:math:`\mathrm{Re}\,X(k)` and to :math:`\mathrm{Im}\,X(k)` and combining them
into power, :math:`P(x) = R(x)^2 + I(x)^2`, produces a 4th-order polynomial
whose true maximum requires solving a cubic. Worse, a 3-point exact fit has
no information about how the spectrum behaves beyond those three points, so
the quartic is not guaranteed to stay single-peaked as :math:`\delta` moves
away from zero. In testing this showed bias exceeding half a bin.

**Fit the power directly (quadratic).** Interpolating
:math:`|X(k)|^2`, which is real, non-negative, and phase-invariant, with a
single quadratic avoids the cubic entirely. The standard 3-point vertex
formula gives the correction directly:

.. math::

   \delta = \frac{1}{2}\,\frac{y_1 - y_3}{y_1 - 2y_2 + y_3},
   \qquad y_i = |X(k_0 + i - 2)|^2

This is the same form used by the firmware's three-tone vertex estimate in
:doc:`/examples/tracking_algorithm`. Fitting the quadratic to the
*logarithm* of the power instead matches a windowed mainlobe's shape more
closely, since no real window is actually parabolic in linear power:

.. math::

   \delta = \frac{1}{2}\,\frac{\ln y_1 - \ln y_3}{\ln y_1 - 2\ln y_2 + \ln y_3}

Why the bias is zero at the center and worst near a quarter bin
------------------------------------------------------------------

The log-power form makes one fact immediate: at :math:`\delta = 0` the
spectrum is symmetric about :math:`k_0`, so :math:`y_1 = y_3` and the
estimator returns exactly zero bias, independent of noise, window, or
:math:`N`. The same symmetry argument applies at :math:`\delta = 0.5`, the
bin boundary. Between these two exact zeros the bias is not zero: it rises,
peaks near a quarter bin, and falls back to zero. For a Hann-windowed,
:math:`N = 2048` estimator this peak is about 0.016 bins, in line with bench
measurements of the same formula.

Comparison against literature estimators
------------------------------------------

The PDF evaluates this quadratic/log-quadratic fit against Jacobsen, Quinn's
2nd estimator, and the Candan corrections, on a noiseless single-tone model
swept across :math:`\delta \in [0, 0.5]`:

.. list-table::
   :header-rows: 1
   :widths: 40 15 20 20

   * - Estimator (window)
     - Peak bias (bin)
     - Peak bias (Hz)
     - Bias @ :math:`\delta=0.25`
   * - Jacobsen, Candan 2011/2013 (rect)
     - 0.000
     - 0.000
     - 0.000
   * - Log-power quadratic (Hann)
     - 0.016
     - 0.078
     - 0.016
   * - Jacobsen, Candan 2011 (Hann)
     - 0.250
     - 1.222
     - -0.125
   * - Quinn's 2nd estimator (Hann)
     - 0.667
     - 3.257
     - -0.500
   * - Quartic Re/Im fit (Hann)
     - 0.625
     - 3.052
     - -0.313

Jacobsen and the Candan corrections are exact for a rectangular window,
because their closed-form derivations assume one. They are not derived for a
Hann window, and carrying the rectangular-window Candan correction factor
over to Hann-windowed bins does not fix anything: the peak bias stays at
0.25 bin, same as plain Jacobsen. On a Hann window, the log-power quadratic
has the smallest peak bias of every estimator tested, over 15x smaller than
Jacobsen and more than 40x smaller than the quartic fit, because
:math:`\ln|X(k)|^2` is close to a genuine parabola near a Hann mainlobe.

Takeaways
----------

- A 3-point fit to the complex spectrum (quartic) is the wrong tool: no
  constraint forces it to decay away from the peak, so it can be worse than
  not correcting at all.
- A quadratic fit to power, or to log-power, is simpler, better motivated
  (phase-invariant, unimodal), and has closed-form bias zeros at the bin
  center and bin boundary by symmetry alone.
- The log-power variant is the better match for a Hann-windowed mainlobe and
  is the smallest-bias option of everything compared here; see the PDF for
  the full derivations, references, and additional figures.
