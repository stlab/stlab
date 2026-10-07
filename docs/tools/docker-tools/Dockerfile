# Build from the latest ubuntu release
# [DEPENDENCY] https://hub.docker.com/_/ubuntu (latest distribution packages)
FROM ubuntu:latest

# Install items requiring root access

ARG DEBIAN_FRONTEND=noninteractive
# [DEPENDENCY] https://packages.ubuntu.com/ (distribution-provided build tools)
RUN apt-get update && apt-get install -y \
  apt-utils \
  cmake \
  doxygen \
  fswatch \
  g++ \
  git \
  libreadline-dev \
  make \
  ninja-build \
  nodejs \
  npm \
  rbenv \
  wget \
  zlib1g-dev

# [DEPENDENCY] https://packages.ubuntu.com/search?keywords=libyaml-dev
RUN apt-get install -y libyaml-dev

# [DEPENDENCY] https://www.npmjs.com/package/n (latest)
RUN npm install -g n
# Turn off XZ compression because 20.5.1 had a broken link
# [DEPENDENCY] https://nodejs.org/en/download (latest)
RUN export N_USE_XZ=0; n latest

# [DEPENDENCY] https://www.npmjs.com/package/npm (latest)
RUN npm install -g npm@latest
# [DEPENDENCY] https://www.npmjs.com/package/browser-sync (latest)
RUN npm install -g browser-sync

# Create a user "app" so everything is not running at root
RUN useradd -ms /bin/bash app
USER app
WORKDIR /home/app

# UTF-8 locale (C.UTF-8 is available on Ubuntu without locale-gen; Jekyll needs UTF-8)
ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8
ENV LANGUAGE=C.UTF-8

# Install ruby environment
RUN echo 'eval "$(rbenv init -)"' >> ~/.bashrc
RUN mkdir -p "$(rbenv root)"/plugins
# [DEPENDENCY] https://github.com/rbenv/ruby-build/releases (tracks upstream HEAD)
RUN git clone https://github.com/rbenv/ruby-build.git "$(rbenv root)"/plugins/ruby-build

#  Install ruby
# [DEPENDENCY] https://www.ruby-lang.org/en/downloads/ (latest stable unless overridden)
ARG RUBY_VERSION
RUN if [ -z ${RUBY_VERSION+x} ]; then \
  rbenv install $(rbenv install -l | grep -v - | tail -1); \
  rbenv global  $(rbenv install -l | grep -v - | tail -1); \
  else \
  rbenv install $RUBY_VERSION; \
  rbenv global $RUBY_VERSION; \
  fi

#  Install bundler in global ruby
# [DEPENDENCY] https://rubygems.org/gems/bundler (latest)
RUN (eval "$(rbenv init -)"; gem install bundler)

# build the final files
# FROM base AS full
USER app
WORKDIR /home/app

EXPOSE 3000 3001

# Add version file last to avoid cache invalidation for minor releases
ADD ./docs/tools/docker-tools/VERSION .
