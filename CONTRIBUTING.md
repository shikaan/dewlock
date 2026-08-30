Contributing
---

## Building from source (local)

Install dependencies:

* wayland
* libxkbcommon
* cairo
* pam (optional)
* scdoc (optional: man pages) \*
* git \*

_\* Compile-time dep_

```sh
make all
```

## Building from source (container)

The project comes with a [Dockerfile](./Dockerfile) containing all the
dependencies required to build the project.

Using `docker` or `podman` you can

```sh
# build the development image
podman build . -t dewlock-dev

# run the development container
podman run --rm -it -v "$(pwd):/src:Z" --name dewlock dewlock-dev
```

## Using LSPs

You can get LSP (clangd) support from the running container like this

```sh
# run the container as above

# run the LSP 
podman exec -i dewlock clangd \
    --background-index \
    --path-mappings=<local-path-to-dewlock>/dewlock=/src"
```
