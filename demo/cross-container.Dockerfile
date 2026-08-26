FROM scratch AS victim
COPY hold /hold
COPY victim-static /victim
COPY victim-constant-static /victim-constant
ENTRYPOINT ["/hold"]

FROM scratch AS attacker
COPY hold /hold
COPY flush /flush
COPY monitor-modern-once /monitor
ENTRYPOINT ["/hold"]
