/*
    Fractal Image Generator

    Author: Alberto Boldrini


    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdio>
#include <complex>
#include <iostream>
#include <cmath>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <png.h>

// Changes the maximum alignment of members of structures.
// No padding space are added to reach words sizes.
#pragma pack(push, 1)

// This structure represents a RGB color 8 bit depth
struct Color
{
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

// Returns to default packing settings
#pragma pack(pop)

// This structure represents a raster Image
// which can be written to a png file 
struct Image
{
    // Create an image with specified dimensions
    Image (int width, int height)
        : width  (width)
        , height (height)
    {
        // Creates a list of rows
        data = new Color* [height];

        // Creates the rows one by one
        for (int i = 0; i < height; i++)
            data[i] = new Color [width * 3];
    }

    ~Image ()
    {
        // Deletes rows one by one
        for (int i = 0; i < height; i++)
            delete[] data[i];

        // Delete the list of rows
        delete[] data;
    }

    // Writes the image to a PNG file
    void write (const char *filename)
    {
        // Opens the output file for writing
        FILE *fp = fopen (filename, "wb");
 
        // Creates PNG data structure
        png_struct *png_ptr  = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        png_info   *info_ptr = png_create_info_struct (png_ptr);

        // The output stream for the PNG data
        png_init_io (png_ptr, fp);

        // Sets information about the image
        png_set_IHDR (png_ptr, info_ptr, width, height,
                      8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                      PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

        // Writes the png data
        png_write_info(png_ptr, info_ptr);
        png_write_image(png_ptr, (png_byte **) data);
        png_write_end(png_ptr, NULL);

        // Removes structures
        png_destroy_write_struct (&png_ptr, &info_ptr);

        // Closes the output file
        fclose(fp);
    }

    // Image data
    Color **data;

    // Dimensions
    int width;
    int height;
};

// This structure represents an image of a fractal which
// must be computed and written to a file.
struct Mandlebrot
{
    // Specifices the resolution and the corners of the image in the complex plane
    Mandlebrot (double resolution, double left, double top, double right, double bottom)

        // Allocates the image data
        : image (int (resolution * (right - left)), int (resolution * (top - bottom)))
        
        // Default color of the body of the fractal
        , bodyColor {0,0,0}

        // Corners of the image in the complex plane
        , left (left)
        , right (right)
        , top (top)
        , bottom (bottom)

        // Parameters for the rendering
        , maxIterations (100)
        , stopNorm (400)
    {
        // Compute slope and intercept
        mSmooth = 1 / log2 (0.5 * log2 (std::norm(step(1e5, 0))) / log2(1e5));
        bSmooth = log2 (0.5 * log2 (stopNorm)) * mSmooth;
    }

    // The step function of the fractal
    static std::complex<double> step (std::complex<double> z, std::complex<double> c)
    {
        return z*z + c;
    }

    // Compute a pixel of the image
    void computePixel (int x, int y)
    {
        // Computes the current position in the complex plane
        std::complex<double> c (left + (right - left) * x / image.width, 
                                top  + (bottom - top) * y / image.height); 

        // The initial point of the sequence
        std::complex<double> z = c;

        // The number of iterations made
        int iN = 0;

        // Iterate until the complex number exits or when many iterations have been made
        while (std::norm(z) < stopNorm && iN++ < maxIterations)
            z = step (z,c);
        
        // Fetches the output color        
        Color &color = image.data[y][x];

        if (iN >= maxIterations)
            color = bodyColor;

        else
        {

            // Computes the number of iterations smoothed using 
            // the last value computed of the sequences 
            double fN = iN + bSmooth - mSmooth * log2 (0.5 * log2 (std::norm (z)));

            // Selects the color used to paint the pixel 
            double nC = (1 - exp (-0.05 * fN)) * (colorList.size() - 1);

            // Selects the index of the first color to use from the list
            unsigned iC = unsigned (nC);

            // Computes fractional part of the color to mix the first with the second
            double fC = nC - iC;

            // Selects the two colors to interpolate
            Color& color1 = colorList[iC];
            Color& color2 = colorList[iC+1];

            // Converts the (linear) fractional part with a smooth function
            double mix = 0.5 * (1 + cos (M_PI * fC));  

            // Writes the color
            color.red   = (unsigned char) (color1.red   * mix + color2.red   * (1-mix));
            color.green = (unsigned char) (color1.green * mix + color2.green * (1-mix));
            color.blue  = (unsigned char) (color1.blue  * mix + color2.blue  * (1-mix));
        }
    }

    // Computes an area of the image
    void computeArea (int leftArea, int topArea, int rightArea, int bottomArea)
    {
        for (int y = topArea; y < bottomArea; y++)
            for (int x = leftArea; x < rightArea; x++)
                computePixel (x, y);                
    }

    // Computes the image using a single core
    void computeSingleCore ()
    {
        // Computes the whole image
        computeArea (0, 0, image.width, image.height);
    }

    // Computes the image using all available cores
    void computeMultiCore ()
    {
        // Gets the numbers of available cores
        unsigned nThreads = std::thread::hardware_concurrency ();

        // A list of thread
        std::vector<std::thread> threads (nThreads);

        // The number of blocks taken and processed
        std::atomic<int> nextBlock = 0;
        std::atomic<int> doneBlock = 0;

        for (unsigned i = 0; i < nThreads; ++i) 
        {
            threads[i] = std::thread([&]
            {
                for (int currentBlock = nextBlock++; currentBlock < 100; currentBlock = nextBlock++)
                {
                    // Computes the block of the image
                    computeArea ((currentBlock+0) * image.width / 100, 0, 
                                 (currentBlock+1) * image.width / 100, image.height); 

                    // Prints the current progress
                    std::cout << "\rProcessing... " << ++doneBlock << "%" << std::flush;
                }
            });
        }

        // Waits all thereads
        for (std::thread& th : threads)
            th.join();

        std::cout << "\n";
    }

    // Image data and informations
    Image image;

    // List of colors in the outside of the fractal
    std::vector<Color> colorList;

    // Color in the inside of the fractal
    Color bodyColor;

    // The corners of the image in the complex plane
    double left, right;
    double top, bottom;

    // Max iterations to consider the point inside the fractal
    int maxIterations;

    // Radius to consider the point definitly outside the fractal
    double stopNorm;
    
    // Precomputed coefficients
    double mSmooth, bSmooth;
};



int main (int argc, char **argv)
{
    Mandlebrot fractal (500, -2.7, +1.25, +1.7, -1.25);
    //Mandlebrot fractal (1000, -1.5, +1.5, +1.5, -1.5);

    // Adds colors to the fractal   
    fractal.colorList.push_back (Color{  0,   0,   40 });
    fractal.colorList.push_back (Color{  0,  50,  100 });
    fractal.colorList.push_back (Color{  0,  200,  0 });
    fractal.colorList.push_back (Color{ 255, 255, 100 });
    fractal.colorList.push_back (Color{ 255, 255, 255 });


    auto start = std::chrono::steady_clock::now ();

    // Computes the image
    fractal.computeMultiCore ();

    auto end = std::chrono::steady_clock::now();

    // Computes the number of seconds taken
    double seconds = std::chrono::duration <double> (end - start).count();

    // How many pixels has this image?
    double pixels = fractal.image.width * fractal.image.height;
    
    // Writes a summary
    std::cout << "Fractal produced in " << seconds << " seconds (" << (seconds * 1e9 / pixels) << " nsec/pixel)" << std::endl;

    // Produces the PNG file
    fractal.image.write ("img/out9.png");
    return 0;
}