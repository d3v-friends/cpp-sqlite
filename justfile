set shell := ["bash", "-cu"]
mod git

line:
    # brew install cloc
    cloc ./include ./source --include-ext=cpp,hpp,c,h,cppm,ippx,inl
