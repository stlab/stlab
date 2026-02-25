# Using the docker image

## Setup

### Install Docker

If you don't already have Docker installed, [install Docker](https://docs.docker.com/get-docker/).

### Building the docker image

To build the docker image, first, update the VERSION variable below (please use semantic versioning). Add a [release note](#release-notes).

Specify the ruby version to match the latest stable - https://www.ruby-lang.org/en/downloads/

macOS and Linux:

```bash
VERSION="1.0.5"
VOLUME="stlab.libraries"
RUBY_VERSION="3.4.4"
```

Windows:

```powershell
$VERSION="1.0.5"
$VOLUME="stlab.libraries"
$RUBY_VERSION="3.4.4"

$PSDefaultParameterValues = @{'Out-File:Encoding' = 'Ascii'}
```

Update the Docker and ruby version in the following files:

```bash
echo $VERSION > ./docs/tools/docker-tools/VERSION
echo $RUBY_VERSION > ./docs/.ruby-version
```

Build the image, no-cache is used so the latest tools are installed

```bash
docker build --build-arg RUBY_VERSION=$RUBY_VERSION --file ./docs/tools/docker-tools/Dockerfile --tag $VOLUME . --no-cache
```

## Running the Docker image with remote theme

To run the docker image, execute the following.

macOS and Linux (Bash):

```bash
docker run --mount type=bind,source="$(pwd)",target=/mnt/host --tty --interactive --publish 3000-3001:3000-3001 $VOLUME bash
```

Windows (PowerShell) — use `$PWD` and quote the whole `--mount` argument so the path is passed correctly:

```powershell
docker run --mount "type=bind,source=$PWD,target=/mnt/host" --tty --interactive --publish 3000-3001:3000-3001 $VOLUME bash
```

### Running the Docker image with local theme

Edit Gemfile and _config.yml to use a local copy of the theme. See `[local-them]` in the files for details.

```bash
code ./docs/Gemfile
code ./docs/_config.yml
```

```
docker run --mount type=bind,source="$(pwd)",target=/mnt/host \
    --mount type=bind,source=`readlink -f ../../adobe/hyde-theme`,target=/mnt/themes \
    --tty --interactive --publish 3000-3001:3000-3001 \
    $VOLUME bash
```

## Preparing the docs

This should leave you at a bash prompt that looks like this:

```
app@fc7590a63ba3:~$
```

The hex number is the docker image container ID and may be different. Going foreword I refer to this as the _docker_ prompt to distinguish it from the _local_ prompt.

```
cd /mnt/host
git config --global --add safe.directory /mnt/host
./docs/tools/docs/update.sh
```

## Build the documentation site

To build or rebuild the complete documentation site locally, execute the following from the docker prompt:

```
cd /mnt/host
./docs/tools/docs/prepare.sh
```

## Run a local server for the site

Once the site has been prepared, you can run it to see how it looks. From the docker prompt enter:

```
cd /mnt/host
./docs/tools/docs/start.sh
```

To view the site, open a browser to `http://localhost:3000`. The site will auto-rebuild and refresh as files are changed.

## Tips

If you want to open another terminal on the running image use:

```
docker ps
docker exec -it <container id> bash
```

### Release Notes

- 1.0.0 - Initial release for Jekyll
- 1.0.1 - Updating tool set
- 1.0.2 - Updating in for Hyde 2.0
- 1.0.3 - Updating Jekyll to 4.2.0 for new Hyde and moving to GitHub Actions.
- 1.0.4 - Updating docs for new header directory structure. The gem installs are no longer baked into the image, this was causing too many issues.
- 1.0.5 - Updating to Ruby 3.4.4 and Jekyll 5.1.0.
