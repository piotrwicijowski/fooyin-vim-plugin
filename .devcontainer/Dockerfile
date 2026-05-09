FROM node:25-trixie

ARG TZ
ENV TZ="$TZ"

ARG CLAUDE_CODE_VERSION=latest

# Install basic development tools and iptables/ipset
RUN apt-get update && apt-get install -y --no-install-recommends \
  less \
  procps \
  sudo \
  man-db \
  unzip \
  aggregate \
  jq \
  vim \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# C++ toolchain for fooyin plugin development
RUN apt-get update && apt-get install -y --no-install-recommends \
  cmake \
  ninja-build \
  g++ \
  clang \
  clangd \
  clang-format \
  clang-tidy \
  pkg-config \
  git \
  qt6-base-dev \
  libqt6svg6-dev \
  libasound2-dev \
  libtag-dev \
  libavcodec-dev \
  libavformat-dev \
  libavutil-dev \
  libavdevice-dev \
  libavfilter-dev \
  libswscale-dev \
  libswresample-dev \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# Ensure default node user has access to /usr/local/share
RUN mkdir -p /usr/local/share/npm-global && \
  chown -R node:node /usr/local/share

ARG USERNAME=node

# Persist bash history.
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  && mkdir /commandhistory \
  && touch /commandhistory/.bash_history \
  && chown -R $USERNAME /commandhistory

# Set `DEVCONTAINER` environment variable to help with orientation
ENV DEVCONTAINER=true

# Create workspace and config directories and set permissions
RUN mkdir -p /workspace /home/node/.claude && \
  chown -R node:node /workspace /home/node/.claude

WORKDIR /workspace

# Set up non-root user
USER node

# Install global packages
ENV NPM_CONFIG_PREFIX=/usr/local/share/npm-global
ENV PATH=$PATH:/usr/local/share/npm-global/bin

# Set the default editor and visual
ENV EDITOR=vim
ENV VISUAL=vim

# Install Claude
RUN curl -fsSL https://claude.ai/install.sh | bash

# Install opencode
RUN curl -fsSL https://opencode.ai/install | bash

# Install Opencode with Claude
RUN npm install -g opencode-with-claude

# Opencode configuration
COPY --chown=node:node opencode.json tui.json /home/node/.config/opencode/

# Install pnpm
RUN curl -fsSL https://get.pnpm.io/install.sh | bash
