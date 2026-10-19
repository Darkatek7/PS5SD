# Build environment for PS5SD: Arch Linux (what PS5_Vulkan supports) with the host
# packages of docker/packages.txt. The PS5 toolchain is x86-64 only, so on Apple
# Silicon or ARM Linux Docker emulates it: correct, but slow (the RADV build takes hours).
#
#   docker build --platform linux/amd64 -t ps5sd-build docker/
#   docker run --rm -it --platform linux/amd64 -v "$PWD":/work ps5sd-build scripts/setup-deps.sh
#
# Then build the title the same way:
#
#   docker run --rm -it --platform linux/amd64 -v "$PWD":/work ps5sd-build scripts/build-app.sh
# The platform comes from --platform on the command line (and the workflow).
FROM archlinux:latest

COPY packages.txt /tmp/packages.txt

# pacman's download sandbox (Landlock) fails under some container runtimes; the
# line is only uncommented when this pacman ships it.
RUN sed -i 's/^#\(DisableSandbox\)/\1/' /etc/pacman.conf && \
    pacman -Syu --noconfirm --needed $(sed 's/#.*//' /tmp/packages.txt | xargs) && \
    pacman -Scc --noconfirm && \
    rm -f /tmp/packages.txt

# The repository is bind-mounted from the host with another owner, and the
# scripts run git in it (and in the clones under .deps).
RUN git config --system --add safe.directory '*'

WORKDIR /work
CMD ["bash"]
