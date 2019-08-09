#!/usr/bin/env python

from setuptools import setup, find_packages

setup(name='midas',
      version='0.0.1',
      description='Python tools for MIDAS (maximum integrated data acquisition system)',
      author='Ben Smith',
      author_email='bsmith@triumf.ca',
      url='https://midas.triumf.ca/MidasWiki/index.php/Main_Page',
      packages = find_packages("midas"),
     )

