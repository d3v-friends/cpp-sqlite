set shell := ["bash", "-cu"]
mod git

line:
    # brew install cloc
    cloc . --match-d='(^|/)(ctls|test)($|/)' --include-ext=cpp,hpp,c,h,cppm,ippx,inl