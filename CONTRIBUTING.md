Contributing
---

The easiest way to work on dewlock is through the provided
[development container](#building-from-source-container).

```sh
make all
```

This builds a release binary with the PAM backend. Use the `AUTH_BACKEND``
parameter for a `shadow` build

```sh
make AUTH_BACKEND=shadow all
```

If you want sanitizers and debug symbols, run a debug build instead:

```sh
make BUILD_TYPE=debug all
```

Use `make help` to list all targets and build parameters.

## Building from source (container)

The project comes with a [Dockerfile](./Dockerfile) containing all the
dependencies required to build the project.

Using `docker` or `podman` you can

```sh
# build the development image
docker build . -t dewlock

# run the development container
docker run --rm -it -v "$(pwd):/src:Z" --name dewlock dewlock
```

## Building from source (local)

Install build tools:
* GNU make \*
* git \*
* a C11 compiler
* scdoc (optional: man pages) \*
* wayland-scanner

Install dependencies:
* wayland-client
* libxkbcommon
* cairo
* pam (optional)

_\* Compile-time dependency_

You can now run any of the make recipe above.

## Using LSPs

For LSP support, run `clangd` in the development container

```sh
# run the container as above

# run the LSP 
podman exec -i dewlock clangd \
    --background-index \
    --path-mappings=<local-path-to-dewlock>/dewlock=/src"
```

## Packaging

Use `make install` for packaging. To build a distribution package, build without
root permissions and stage the files into a temporary directory:

```sh
make AUTH_BACKEND=pam VERSION="$pkgver" all
make AUTH_BACKEND=pam install DESTDIR="$pkgdir" PREFIX=/usr
```

The `BUILD_TYPE` and `AUTH_BACKEND` values select the binary. Use the same
values for the `install` target that you used for the `all` target. 

This procedure creates these files:

```
$pkgdir/usr/bin/dewlock
$pkgdir/usr/share/man/man1/dewlock.1
$pkgdir/usr/share/bash-completion/completions/dewlock
$pkgdir/usr/share/zsh/site-functions/_dewlock
$pkgdir/usr/share/fish/vendor_completions.d/dewlock.fish
$pkgdir/etc/pam.d/dewlock
```

Mark `/etc/pam.d/dewlock` as a configuration file. This makes sure that
local edits to the file survive future upgrades.
