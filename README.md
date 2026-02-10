# BeTon

A native music player for Haiku.

<img width="578" height="371" alt="grafik" src="https://github.com/user-attachments/assets/07e42d3a-788a-4c2b-be07-dbd08c9da8a2" />


![Haiku](https://img.shields.io/badge/Platform-Haiku-blue)
![License](https://img.shields.io/badge/License-MIT-green)


## Features

*   Audio playback
*   Playlist creation
*   Reads and writes tags, with bfs attribute synchronization
*   MusicBrainz metadata lookup
*   Color support, just drop a color on the seekbar

## Development

## Acknowledgements

*   Special thanks to **zuMi** for creating all the brilliant icons, used in this project.

## Build Requirements

### System Dependencies

Install via `pkgman`:

```bash
pkgman install taglib_devel musicbrainz_devel
```

## Building

```bash
cd BeTon
make
make bindcatalogs
```

## Documentation

Generate API docs with Doxygen:

```bash
pkgman install doxygen graphviz
doxygen Doxyfile
```

Open `docs/html/index.html` in WebPositive.

## License

MIT License - See [LICENSE](LICENSE) for details.
