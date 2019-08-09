## Midas python tools

This directory contains midas tools written in python. At the moment we just provide a file reader to enable pythonic analysis of midas files. More tools may be added in future.

This code supports both python 2.7 and python 3.

### Installing

We highly recommed using `pip` and/or `virtualenv` to manage your python packages, but you can also just edit your paths if desired.

#### Installing using pip

`pip` allows you to install packages from online repositories (e.g. `pip install lz4` will download and install the `lz4` package from the PyPi online repository), and also allows you to install packages you already have on disk.

`virtualenv` allows you to set up multiple virtual python environments, so you can use, for example, one set of packages when working
on experiment A, and a second set of packages when working on experiment B. Without virtualenv you can quickly get into
"dependency hell" where different experiments have conflicting requirements.

To install pip:

```bash
wget https://bootstrap.pypa.io/get-pip.py
python get-pip.py --user
```

To install virtualenv (requires pip):

```bash
pip install virtualenv --user
pip install virtualenvwrapper --user
source ~/.local/bin/virtualenvwrapper.sh # Also add this to your .bashrc or similar
mkvirtualenv DEMO # You can call your environment anything you want

# You are now in your virtual python environment. 
# You can leave it by running `deactivate`.
# You can re-enter it by running `workon DEMO` (or whatever you called your venv).
# You can create multiple virtualenvs, and even specify different python versions for each.
```

To install this package:

```bash
pip install -e /path/to/midas/python --user
# The -e flag makes this an "editable" install. If you upgrade midas in future,
# python will automatically see the new code without you having to re-install.
```

You can now use `import midas` and `import midas.file_reader` in your python scripts.

#### Installing by editing paths

If you don't want to use `pip`, you can edit your environment variables so python knows where to find the midas module.

You can either do this in your shell, before you run `python`:

```bash
export PYTHONPATH=$PYTHONPATH:/path/to/midas/python
```

Or you can do it from within your python program itself:

```python
import sys
sys.path.append("/path/to/midas/python")
import midas
```