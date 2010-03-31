all: dbus-daemon-proxy

dbus-daemon-proxy: dbus-daemon-proxy.c
	$(CC) `pkg-config --cflags --libs dbus-1 dbus-glib-1` -o $@ $<

clean:
	rm -f dbus-daemon-proxy
