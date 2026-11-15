# Default: WeAct G431 Candlelight. Other upstream makefiles remain available.
include Make_G431_Candle_WeActStudio

.PHONY: test test-host verify
test:
	python3 -m unittest discover -s Tests -v

test-host:
	python3 -m unittest discover -s tests_host -v

verify:
	python3 tools/verify_release.py
