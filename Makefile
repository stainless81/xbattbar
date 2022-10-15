OS_TYPE		?=	linux
PROJECT		=	xbattbar
DESTDIR		?=	/

TARGET		=	xbattbar
APM_CHECK	=	xbattbar-check-apm
CPPFLAGS	=	-D_FORTIFY_SOURCE=2
CFLAGS		=	-g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security $(CPPFLAGS)
LDFLAGS		=	-Wl,-z,relro

all: $(TARGET) $(APM_CHECK)

$(TARGET): obj/xbattbar.o
	gcc -o $@ $< -lX11 $(LDFLAGS)

obj/xbattbar.o: xbattbar.c obj/stamp
	gcc -MMD -o $@ -c $< $(CFLAGS)

$(APM_CHECK): obj/xbattbar-check-apm.o
	gcc -o $@ $< $(LDFLAGS)

obj/xbattbar-check-apm.o: xbattbar-check-apm.c obj/stamp
	gcc -MMD -D$(OS_TYPE) -o $@ -c $< $(CFLAGS)

obj/stamp:
	mkdir obj
	touch $@

clean:
	rm -fr obj
	rm -f $(TARGET) $(APM_CHECK)


install: $(TARGET) $(APM_CHECK)
	install -d -m 0755 $(DESTDIR)/usr/lib/$(PROJECT)
	install -d -m 0755 $(DESTDIR)/usr/bin
	install -d -m 0755 $(DESTDIR)/usr/share/man/man1
	install -m 0755 $(APM_CHECK) $(DESTDIR)/usr/lib/$(PROJECT)/
	install -m 0755 xbattbar-check-acpi $(DESTDIR)/usr/lib/$(PROJECT)/
	install -m 0755 xbattbar-check-sys  $(DESTDIR)/usr/lib/$(PROJECT)/
	install -m 0755 $(TARGET) $(DESTDIR)/usr/bin/
	install -m 0644 xbattbar.man $(DESTDIR)/usr/share/man/man1/$(PROJECT).1 

include $(wildcard obj/*.d) 
