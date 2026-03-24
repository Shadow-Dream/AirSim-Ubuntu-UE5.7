from setuptools import setup


setup(
    name="airsim-ubuntu-ue5-7",
    version="1.8.1.post2",
    description="Standalone AirSim Python client for the Ubuntu UE5.7 workspace",
    long_description=open("README.md", encoding="utf-8").read(),
    long_description_content_type="text/markdown",
    author="Shadow-Dream",
    license="MIT",
    python_requires=">=3.10,<3.13",
    packages=["airsim"],
    include_package_data=False,
    install_requires=[
        "numpy==2.2.6",
        "msgpack-python==0.5.6",
        "msgpack-rpc-python==0.4.1",
        "tornado==4.5.3",
        "backports.ssl-match-hostname>=3.7.0.1",
    ],
    extras_require={
        "opencv": ["opencv-python-headless>=4.8"],
    },
    url="https://github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7",
)
