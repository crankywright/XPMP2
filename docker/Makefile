# XPMP2 - Docker only set up to compile Linux
# platforms=win mac lin
platforms=lin mac
container=xpmp2-cross-compile-env

# all: mac lin win
all: $(platforms)

.image: Dockerfile build.sh
	docker build . -t $(container)
	docker inspect -f "{{ .ID}}" $(container):latest > .image

bash: .image
	docker run -it --rm -v "$(realpath ..):/Src" --entrypoint bash $(container):latest

$(platforms): .image
	docker run -i --rm -v "$(realpath ..):/Src" $(container):latest $@

.PHONY: $(platforms) bash
