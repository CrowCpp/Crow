This page shows how you can get started with a simple hello world application using corax template.

## 1. Get the template
First, you need to get the corax template. You can do this by downloading the [corax template repository](https://github.com/seobryn/corax-template) as Zip file.

## 2. Unzip the template
Then, you need to unzip the template and rename the folder with your project name, for example, `hello-world` in this case.

## 3. Compile the code

``` bash
cd hello-world
mkdir build
cd scripts && ./make.sh
./compile.sh
cd ../release/bin/ && ./corax
```

After building your `.cpp` file and running the resulting executable, you should be able to access your endpoint at [http://localhost:8081](http://localhost:8081). Opening this URL in your browser will show a white screen with "Hello world" typed on it.

## 4. Change the code

You can change the code in the `src/main.cpp` file to fit your needs.
