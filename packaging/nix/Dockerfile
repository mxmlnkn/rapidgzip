FROM nixos/nix

RUN nix-channel --update

# Checkout the latest unstable branch to hopefully enable cached builds
# https://channels.nixos.org/nixpkgs-unstable/git-revision
RUN git clone --depth 1 https://github.com/NixOS/nixpkgs.git && cd nixpkgs && \
    git fetch --depth 1 origin bfa8b30043892dc2b660d403faa159bab7b65898 && \
    git checkout bfa8b30043892dc2b660d403faa159bab7b65898
# Set up Nixpkgs for cached builds
#RUN nix-env -iA cachix -f https://cachix.org/api/v1/install && cachix use nixpkgs
RUN nix-env -iA nixpkgs.vim nixpkgs.gnused nixpkgs.wget

CMD ["bash"]
