FROM scratch AS victim
COPY --chmod=0555 hold /hold
COPY --chmod=0555 victim-static /victim
COPY --chmod=0555 victim-constant-static /victim-constant
ENTRYPOINT ["/hold"]

FROM scratch AS attacker
COPY --chmod=0555 hold /hold
COPY --chmod=0555 flush /flush
COPY --chmod=0555 monitor-modern-once /monitor
ENTRYPOINT ["/hold"]
