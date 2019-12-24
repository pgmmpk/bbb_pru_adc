from setuptools import setup, find_packages
import glob

with open('README.md') as f:
    long_description = f.read()

setup(
    name='bbb_pru_adc',
    version='1.0.0',

    description='Streaming capture of ADC on BeagleBone (Black)',
    long_description=long_description,
    long_description_content_type='text/markdown',
    url='https://github.com/pgmmpk/bbb_pru_adc',

    author='Mike Kroutikov',
    author_email='pgmmpk@gmail.com',

    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
    ],
    keywords='beaglebone black adc pru',
    packages=['bbb_pru_adc'],
    python_requires='>=3.6, <4',
    package_data={'bbb_pru_adc': ['resources/*']},
    data_files=[
        ('src', glob.glob('src/*')),
    ],
)