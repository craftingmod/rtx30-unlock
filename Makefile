REPORT ?=
PROM ?=
PROFILE ?= profiles/board.json
OUTPUT ?= build/probe-usb
EXPERIMENTAL ?= 0
ifeq ($(EXPERIMENTAL),1)
UNLOCK_FLAGS = -Experimental
endif

.PHONY: probe profile test unlock probe-usb
probe:
	bun tools/build-probe.ts

profile:
	python tools/probe-to-profile.py --report "$(REPORT)" --prom "$(PROM)" --output "$(PROFILE)"

probe-usb: probe
	python tools/package_probe_usb.py --output "$(OUTPUT)"

test:
	bun tools/test.ts

unlock:
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build-unlock.ps1 -Profile "$(PROFILE)" $(UNLOCK_FLAGS)
