.PHONY: all clean pdk triangle triangle-gasp triangle_strip sprite

all: triangle triangle-gasp triangle_strip sprite

pdk:
	$(MAKE) -C pdk all

triangle: pdk
	$(MAKE) -C triangle all

triangle-gasp: pdk
	$(MAKE) -C triangle-gasp all

triangle_strip: pdk
	$(MAKE) -C triangle_strip all

sprite: pdk
	$(MAKE) -C sprite all

clean:
	$(MAKE) -C pdk clean
	$(MAKE) -C triangle clean
	$(MAKE) -C triangle-gasp clean
	$(MAKE) -C triangle_strip clean
	$(MAKE) -C sprite clean