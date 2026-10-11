<a id="readme-top"></a>

<!-- PROJECT SHIELDS -->
<div align="center">

[![License: GPL-3.0](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](LICENSE)
[![C11](https://img.shields.io/badge/C-C11-A8B9CC.svg?style=for-the-badge&logo=c&logoColor=black)](https://en.cppreference.com/w/c/11)
[![GTK 4](https://img.shields.io/badge/GTK-4-4A86CF.svg?style=for-the-badge&logo=gtk&logoColor=white)](https://www.gtk.org)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3%20Core-5586A4.svg?style=for-the-badge&logo=opengl&logoColor=white)](https://www.opengl.org)
[![Platform](https://img.shields.io/badge/Platform-Linux-FCC624.svg?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org)
[![Repo](https://img.shields.io/badge/GitHub-XENON--github%2Fhydrogen--sim-181717.svg?style=for-the-badge&logo=github)](https://github.com/XENON-github/hydrogen-sim)

</div>
  <p align="center">
    Interactive 3D Electron Density & Wavefunction Viewer in C, GTK 4 and OpenGL
    <br />
    <br />
    <a href="#features">Features</a>
    &middot;
    <a href="#getting-started">Getting Started</a>
    &middot;
    <a href="#usage">Usage & Controls</a>
    &middot;
    <a href="#sample-output">Sample Output</a>
    &middot;
    <a href="#roadmap">Roadmap</a>
    &middot;
    <a href="#license">License</a>
  </p>
</div>

---

<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
      <ul>
        <li><a href="#features">Features</a></li>
        <li><a href="#built-with">Built With</a></li>
      </ul>
    </li>
    <li><a href="#screenshots">Screenshots</a></li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#installation">Installation</a></li>
        <li><a href="#uninstallation">Uninstallation</a></li>
      </ul>
    </li>
    <li>
      <a href="#usage">Usage</a>
      <ul>
        <li><a href="#launching">Launching</a></li>
        <li><a href="#controls--options">Controls & Options</a></li>
        <li><a href="#examples">Examples</a></li>
      </ul>
    </li>
    <li><a href="#sample-output">Sample Output</a></li>
    <li><a href="#how-it-works">How It Works</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>

<!-- ABOUT THE PROJECT -->

## About The Project

**Hydrogen Atom Simulator** is a GPU-accelerated desktop application that visualises the exact quantum-mechanical solutions of the hydrogen atom (and hydrogen-like ions). It shows two linked views of the same state: the **electron probability density** |ψ|² and the **wavefunction ψ** itself, with amplitude and phase. Millions of Monte-Carlo samples are drawn from the true distribution, then rendered as soft glowing particles with HDR bloom.

| View | What it shows |
| :--- | :--- |
| **Electron Density |ψ|²** (main window) | Where the electron is most likely to be found |
| **Wavefunction ψ** (second window) | Amplitude **and** phase: +/− sign for real orbitals, a hue wheel for complex ones |

### Features

- **Quantum State Control:**
  - **Any Orbital:** n = 1&ndash;8, 0 &le; l &lt; n, &minus;l &le; m &le; l, plus one-click presets (1s &hellip; 7i).
  - **Real or Complex Basis:** Chemist's real orbitals (p<sub>x</sub>, d<sub>z²</sub>, f<sub>xyz</sub> &hellip;) or complex Y<sub>l</sub><sup>m</sup> eigenstates.
  - **Nuclear Charge Z:** Hydrogen-like ions such as He&#8314; and Li&sup2;&#8314; (orbitals shrink as 1/Z).
- **Live Physics Readout:**
  - Energy, &lang;r&rang;, most probable radius, 90 % probability radius, |L|, L<sub>z</sub>, degeneracy.
  - Radial and angular node counts with **node radii**, plus a radial-probability plot r&sup2;|R(r)|&sup2;.
- **Rendering:**
  - Round soft particles (or lit solid spheres), HDR pipeline with **bloom**, and a soft procedural nucleus glow.
  - Five colour schemes (incl. colour-blind-safe and phase wheel) that switch live, with no resampling.
  - **Cross-sections:** half-space cut or thin slab along x, y or z to look inside the cloud.
  - **Phase animation** e<sup>&minus;iEt/&hbar;</sup>: real orbitals oscillate in sign, complex ones cycle in hue.
- **Readable Scene:** axes with a&#8320; tick labels, grid, orientation gizmo, colour legends, optional 90 % probability sphere and bounding box.
- **Navigation:** orbit / pan / zoom (mouse and touchpad pinch), view presets, linked cameras, auto-rotate.
- **Export:** PNG screenshots of both views, overlay included.
- **Fast:** multi-threaded, cancellable sampler (2 million points &asymp; 0.3 s per core); colour mapping and slicing run on the GPU; frames are only drawn when something changes.

### Built With

- **Language:** C (GNU C11)
- **UI Toolkit:** GTK 4
- **Graphics:** OpenGL 3.3 Core (GLAD loader), GLSL 3.30
- **Overlay & Export:** Cairo
- **Concurrency:** POSIX Threads (`pthread`)
- **Build System:** GNU Make + `pkg-config`

<!-- SCREENSHOTS -->

## Screenshots

> Drop your own images into `docs/images/` using the same file names (or edit the paths below).

<div align="center">

<!-- 1. Main window: density view -->
<img src="docs/images/density-view.png" alt="Electron density view" width="90%">
<br /><em>Electron density |ψ|² of an orbital</em>
<br /><br />

<!-- 2. Wavefunction window -->
<img src="docs/images/wave-view.png" alt="Wavefunction view" width="90%">
<br /><em>Wavefunction ψ with +/− lobes</em>
<br /><br />

<!-- 3. Side-by-side: sidebar + complex phase wheel -->
<img src="docs/images/sidebar.png" alt="Sidebar controls" width="38%">
<img src="docs/images/complex-phase.png" alt="Complex orbital phase wheel" width="58%">
<br /><em>Sidebar controls (left) and a complex orbital's phase wheel (right)</em>
<br /><br />

<!-- 4. Cross-section -->
<img src="docs/images/cross-section.png" alt="Cross-section slicing" width="90%">
<br /><em>Cross-section slicing to look inside an orbital</em>

</div>

<!-- GETTING STARTED -->

## Getting Started

### Prerequisites

You will need a C compiler, `make`, `pkg-config`, the GTK 4 development files (which also provide Cairo), and a GPU driver with OpenGL 3.3 core support. Mesa software rendering also works, just slowly.

#### Arch Linux
```sh
sudo pacman -S base-devel pkgconf gtk4
```

#### Debian / Ubuntu / Kali Linux
```sh
sudo apt update
sudo apt install build-essential pkg-config libgtk-4-dev
```

#### Fedora / RHEL
```sh
sudo dnf install gcc make pkgconf-pkg-config gtk4-devel
```

### Installation

```sh
# 1. Clone the repository
git clone https://github.com/XENON-github/hydrogen-sim.git
cd hydrogen-sim

# 2. Compile the binary
make

# 3. Run it
./build/hydrogen_simulator
```

Optionally verify the physics and sampler with the GTK-free unit tests:

```sh
make test
```

### Uninstallation

Nothing is installed system-wide. To remove build artifacts:

```sh
make clean
```

<!-- USAGE -->

## Usage

### Launching

```sh
./build/hydrogen_simulator
```

Two windows open: the main window (sidebar + density view) and the wavefunction window. Closing the wavefunction window only hides it; bring it back from **Windows & export** in the sidebar.

### Controls & Options

#### Mouse

| Action | Effect |
| :--- | :--- |
| Drag | Orbit the camera |
| `Shift`+Drag / Right-drag / Middle-drag | Pan |
| Scroll wheel / Touchpad pinch | Zoom |

#### Keyboard

| Key | Action |
| :---: | :--- |
| `R` / `1` | Isometric view (reset) |
| `2` / `3` / `4` | Front / Side / Top view |
| `Space` | Toggle auto-rotate |
| `P` | Toggle phase animation |
| `G` / `A` / `N` | Toggle grid / axes / nucleus glow |
| `B` | Toggle bloom |
| `H` | Toggle info panel & stats |
| `S` | Save PNG screenshots of both views |

#### Sidebar Sections

| Section | Contents |
| :--- | :--- |
| **Orbital** | Preset, real/complex basis, n, l, m, nuclear charge Z |
| **Physics** | Energy, &lang;r&rang;, node radii, angular momentum, radial-probability plot |
| **Sampling** | Particle count (10 k &ndash; 4 M), resample with a new random seed |
| **Appearance** | Colour scheme, particle style, size, brightness, bloom, nucleus glow |
| **Cross-section** | Off / half-space cut / thin slab, axis, position, thickness |
| **Scene overlays** | Grid, axes, nucleus, 90 % sphere, bounding box, info panel, gizmo, legend |
| **Animation & camera** | Auto-rotate, phase animation, camera linking, view presets |
| **Windows & export** | Show/hide wavefunction window, save PNGs |

#### Developer Environment Variables

| Variable | Description |
| :--- | :--- |
| `HYDROGEN_ORBITAL=n,l,m,complex,Z` | Start with a given state (e.g. `4,2,0,0,1`) |
| `HYDROGEN_SAMPLES=<count>` | Initial particle count |
| `HYDROGEN_SCHEME=<0-4>` / `HYDROGEN_STYLE=<0-1>` | Initial colour scheme / particle style |
| `HYDROGEN_SINGLE=1` | Do not open the second window |
| `HYDROGEN_AUTOSHOT=1` | Save screenshots of both views and quit |

### Examples

**Look inside a 3s orbital with a cross-section:**
Select preset `3s`, set *Cross-section &rarr; Half-space cut*, and drag the *Position* slider.

**Watch a complex orbital's phase rotate:**
Set *Basis &rarr; Complex eigenstates*, pick `3d` with m = +1, choose the *Phase wheel* colour scheme, and press `P`.

**Explore a helium ion:**
Set *Nuclear charge Z* to 2 and compare the orbital size and energy against Z = 1.

**Launch straight into a specific state:**
```sh
HYDROGEN_ORBITAL=4,2,0,0,1 HYDROGEN_SAMPLES=1000000 ./build/hydrogen_simulator
```

<!-- SAMPLE OUTPUT -->

## Sample Output

The **Physics** panel for the 4d<sub>z²</sub> orbital (`HYDROGEN_ORBITAL=4,2,0,0,1`):

```text
4d_z²   n=4  l=2  m=+0  ·  Z=1
Energy         E = -0.850 eV   (16-fold degenerate)
Mean radius    <r> = 21.00 a₀    Peak  r = 21.21 a₀
90% of probability inside r = 30.31 a₀
Angular momentum  |L| = 2.449 ħ,  Lz = 0 ħ
Nodes          1 radial at r = 12.00 a₀ · 2 angular
```

Screenshots (`S`) are saved to the working directory as
`hydrogen_n<N>_l<L>_m<M>[c]_<density|wave>_<timestamp>.png` (a `c` suffix marks the complex basis).

<!-- HOW IT WORKS -->

## How It Works

1. **Exact Monte-Carlo Sampling:** The wavefunction factorises as &psi; = R<sub>nl</sub>(r) &middot; Y<sub>l</sub><sup>m</sup>(&theta;,&phi;). The radius is drawn by inverting the cumulative distribution of r&sup2;|R|&sup2;, and the direction by rejection sampling on |Y|&sup2;. This wastes almost no samples, unlike rejection sampling inside a bounding cube. Work is split across threads with independent random generators and can be cancelled instantly when you change orbital.
2. **Compact GPU Data:** Each point is a 16-byte vertex (position, normalised amplitude, phase). Colour mapping, cross-section clipping, phase animation and particle sizing all happen in the vertex shader, so appearance changes are instant.
3. **HDR Rendering:** Particles are blended additively into a half-float framebuffer. A multi-level downsample/upsample bloom pass and tone mapping produce the final image, with energy-conserving particle sizes so brightness stays consistent across zoom levels and sample counts.
4. **Cairo Overlay:** Axes, ticks, legends and the info panel are drawn with Cairo on top of the GL view (and composited into exported PNGs).
5. **Verified Physics:** `make test` checks radial and angular normalisation, the analytic &lang;r&rang; = &frac12;a(3n&sup2; &minus; l(l+1)), real-orbital orientation (p<sub>x</sub> positive along +x), and sampler statistics.

### Code Layout

```text
src/hydrogen.c      physics: R_nl, Y_lm, radial tables, derived properties
src/sampling.c      parallel Monte-Carlo sampler
src/renderer.c      OpenGL pipeline (points, grid, nucleus, bloom, tone map) + embedded GLSL
src/hud.c           Cairo overlay and sidebar radial plot
src/view.c          one 3D viewport: GL area, input, async sampling, frame loop
src/ui.c            sidebar controls
src/app.c           application state, shortcuts, window setup
tests/              physics and sampler unit tests
docs/images/        README screenshots (replace the placeholders)
```

<!-- ROADMAP -->

## Roadmap

* [x] Arbitrary n, l, m with real and complex orbital bases
* [x] Hydrogen-like ions (nuclear charge Z)
* [x] Multi-threaded exact Monte-Carlo sampler
* [x] HDR rendering with bloom and soft round particles
* [x] Live colour schemes, including colour-blind-safe and phase wheel
* [x] Cross-section slicing and phase animation
* [x] Axes, ticks, legends, gizmo and PNG export
* [ ] Superposition states with time evolution
* [ ] Isosurface rendering
* [ ] Energy-level diagram and transition visualisation
* [ ] Video / GIF export
* [ ] Windows and macOS support

<!-- CONTRIBUTING -->

## Contributing

Contributions are welcome! If you would like to help improve the simulator:

1. Fork the Project (`https://github.com/XENON-github/hydrogen-sim`)
2. Create your Feature Branch:
   ```sh
   git checkout -b feature/AmazingFeature
   ```
3. Run `make test` and make sure it passes
4. Commit your Changes:
   ```sh
   git commit -m "Add AmazingFeature"
   ```
5. Push to the Branch:
   ```sh
   git push origin feature/AmazingFeature
   ```
6. Open a Pull Request

<!-- LICENSE -->

## License

Distributed under the **GNU General Public License v3.0**. See [`LICENSE`](LICENSE) for details.

<!-- ACKNOWLEDGMENTS -->

## Acknowledgments

* [GTK 4](https://www.gtk.org) and [Cairo](https://www.cairographics.org) documentation
* [GLAD](https://github.com/Dav1dde/glad) OpenGL loader generator
* Standard quantum-mechanics treatments of the hydrogen atom (radial Laguerre solutions and spherical harmonics)
* GNU Compiler Collection (`gcc`) & GNU Make

<p align="right">(<a href="#readme-top">back to top</a>)</p>
