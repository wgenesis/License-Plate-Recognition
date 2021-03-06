#include "stdafx.h"
#include <opencv2\opencv.hpp>
#include <vector>
#include <iostream> 
#include <algorithm> 
#include <fstream>
#include <string>
#include <opencv2\ximgproc.hpp>

using namespace cv;
using namespace std;
using namespace ml;

Ptr<ml::ANN_MLP> ann_;
Ptr<ml::ANN_MLP> lann_;
String folder = "./dataset";
String annXML = "./model/model.xml";
String loadImage = "./test/雾车牌.jpg";
const int kNeurons = 40;
const int kAnnInput = 440;
const int numNeurons = 20;
const int kCharactersNumber = 65;

static const char *kChars[] =
{
	"0", "1", "2",
	"3", "4", "5",
	"6", "7", "8",
	"9",/*  10  */
	"A", "B", "C",
	"D", "E", "F",
	"G", "H",
	"J", "K", "L",
	"M", "N",
	"P", "Q", "R",
	"S", "T", "U",
	"V", "W", "X",
	"Y", "Z",/*  24  */
	"zh_cuan" , "zh_e"    , "zh_gan"  ,
	"zh_gan1" , "zh_gui"  , "zh_gui1" ,
	"zh_hei"  , "zh_hu"   , "zh_ji"   ,
	"zh_jin"  , "zh_jing" , "zh_jl"   ,
	"zh_liao" , "zh_lu"   , "zh_meng" ,
	"zh_min"  , "zh_ning" , "zh_qing" ,
	"zh_qiong", "zh_shan" , "zh_su"   ,
	"zh_sx"   , "zh_wan"  , "zh_xiang",
	"zh_xin"  , "zh_yu"   , "zh_yu1"  ,
	"zh_yue"  , "zh_yun"  , "zh_zang" ,
	"zh_zhe"/*  31  */
};

typedef struct Pixel
{
	int x;
	int y;
	int value;
}Pixel;

bool cmp(const Pixel&x, const Pixel&y)
{
	return x.value > y.value;
}

bool cmpR(const Rect&x, const Rect&y)
{
	return x.x < y.x;
}

Mat minfilter(Mat&I, int windowsSize)
{
	int size = (windowsSize - 1) / 2;
	int nc = I.cols;
	int nr = I.rows;
	Mat minFilter(I.rows, I.cols, CV_8UC1);
	Mat I_Border((I.rows + windowsSize - 1), (I.cols + windowsSize - 1), CV_8UC1);
	copyMakeBorder(I, I_Border, size, size, size, size, BORDER_CONSTANT, 0);
	for (int i = 0; i < nr; i++)
	{
		for (int j = 0; j < nc; j++)
		{
			double minData = 0;
			Rect roi(j, i, windowsSize, windowsSize);
			Mat Roi_I = I_Border(roi).clone();
			minMaxLoc(Roi_I, &minData, NULL, NULL, NULL);
			minFilter.at<uchar>(i, j) = minData;
		}
	}
	return minFilter;
}

Mat getdarkChannel(Mat&I, int windowSize)
{
	Mat darkImage(I.rows, I.cols, CV_8UC1);
	int nc = I.cols;
	int nr = I.rows;
	int b, g, r;
	int min = 255;
	if (I.isContinuous())
	{
		nc = nr * nc;
		nr = 1;
	}
	for (int i = 0; i < nr; i++)
	{
		uchar* inData = I.ptr<uchar>(i);
		uchar*outData = darkImage.ptr<uchar>(i);
		for (int j = 0; j < nc; j++)
		{
			b = *inData++;
			g = *inData++;
			r = *inData++;
			min = min > b ? b : min;
			min = min > g ? g : min;
			min = min > r ? r : min;
			*outData++ = min;
			min = 255;
		}
	}
	darkImage = minfilter(darkImage, windowSize).clone();
	return darkImage;
}

int* getatmospheric_Light(Mat&darkImage, Mat&Image, int windowSize)
{
	int r = (windowSize - 1) / 2;
	int nr = darkImage.rows;
	int nc = darkImage.cols;
	int darkSize = nr * nc;
	int Top = 0.001*darkSize;
	int *A = new int[3];
	int sum[3] = { 0,0,0 };
	Pixel *toppixels, *allpixels;
	toppixels = new Pixel[Top];
	allpixels = new Pixel[darkSize];
	for (int i = 0; i < nr; i++)
	{
		const uchar*outData = darkImage.ptr<uchar>(i);
		for (int j = 0; j < nc; j++)
		{
			allpixels[i*nc + j].value = *outData;
			allpixels[i*nc + j].x = i;
			allpixels[i*nc + j].y = j;
		}
	}
	std::sort(allpixels, allpixels + darkSize, cmp);
	memcpy(toppixels, allpixels, (Top) * sizeof(Pixel));
	int val0, val1, val2, avg, max = 0, maxi, maxj, x, y;
	for (int i = 0; i < Top; i++)
	{
		x = allpixels[i].x; y = allpixels[i].y;
		const uchar*outData = Image.ptr<uchar>(x);
		outData += 3 * y;
		val0 = *outData++;
		val1 = *outData++;
		val2 = *outData++;
		avg = (val0 + val1 + val2) / 3;
		if (max < avg) { max = avg; maxi = x; maxj = y; }
	}
	for (int i = 0; i < 3; i++)
	{
		A[i] = Image.at<Vec3b>(maxi, maxj)[i];
	}
	return A;
}

Mat getTransmission_dark(Mat&Image, Mat&darkImage, int*array, int windowSize)
{
	float avg_A = (array[0] + array[1] + array[2]) / 3.0;
	float w = 0.95;
	int r = (windowSize - 1) / 2;
	int nr = Image.rows, nc = Image.cols;
	Mat transmission(nr, nc, CV_32FC1);
	for (int k = 0; k < nr; k++)
	{
		const uchar*inData = darkImage.ptr<uchar>(k);
		for (int l = 0; l < nc; l++)
		{
			transmission.at<float>(k, l) = 1 - w * (*inData++ / avg_A);
		}
	}
	Mat trans(nr, nc, CV_32FC1);
	Mat graymat(nr, nc, CV_8UC1);
	Mat garymat_32F(nr, nc, CV_32FC1);
	cvtColor(Image, graymat, CV_BGR2GRAY);
	for (int i = 0; i < nr; i++)
	{
		uchar*inData = graymat.ptr<uchar>(i);
		for (int j = 0; j < nc; j++)
			garymat_32F.at<float>(i, j) = inData[j] / 255.0;
	}
	cv::ximgproc::guidedFilter(garymat_32F, transmission, trans, 6 * windowSize, 0.001);
	return trans;
}

Mat recover(Mat&Image, Mat&t, int*array, int windowSize)
{
	int test;
	int r = (windowSize - 1) / 2;
	int nr = Image.rows;
	int nc = Image.cols;
	float tx = t.at<float>(r, r);
	float t0 = 0.1;
	Mat outImage = Mat::zeros(nr, nc, CV_8UC3);
	int val = 0;
	for (int i = 0; i < 3; i++)
	{
		for (int k = r; k < nr - r; k++)
		{
			const float*inData = t.ptr<float>(k);
			inData += r;
			const uchar*srcData = Image.ptr<uchar>(k);
			srcData += r * 3 + i;
			uchar*outData = outImage.ptr<uchar>(k);
			outData += r * 3 + i;
			for (int l = r; l < nc - r; l++)
			{
				tx = *inData++;
				tx = tx > t0 ? tx : t0;
				val = (int)((*srcData - array[i]) / tx + array[i]);
				srcData += 3;
				*outData = val > 255 ? 255 : val;
				outData += 3;
			}
		}
	}
	return outImage;
}

Mat cleanFog(Mat Image)
{
	Mat darkImage(Image.rows, Image.cols, CV_8UC1);
	Mat trans(Image.rows, Image.cols, CV_32FC1);
	Mat outImage(Image.rows, Image.cols, CV_8UC3);
	int MinSize;
	int windowSize;
	Image.rows < Image.cols ? MinSize = Image.rows : MinSize = Image.cols;
	windowSize = MinSize / 33;
	if (windowSize % 2 == 0)
		windowSize++;
	int *A = new int[3];
	cout << "求暗通道中..." << endl;
	darkImage = getdarkChannel(Image, windowSize).clone();
	cout << "求大气光值A中..." << endl;
	A = getatmospheric_Light(darkImage, Image, windowSize);
	cout << "求透视图中..." << endl;
	trans = getTransmission_dark(Image, darkImage, A, windowSize).clone();
	cout << "计算中..." << endl;
	outImage = recover(Image, trans, A, windowSize).clone();
	cout << "裁剪边框..." << endl;
	Mat grayImage;
	cvtColor(outImage, grayImage, CV_RGB2GRAY);
	threshold(grayImage, grayImage, 0, 255, CV_THRESH_OTSU + CV_THRESH_BINARY);
	vector< vector<Point> > contours;
	findContours(grayImage, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE);
	vector< vector<Point> >::iterator itc = contours.begin();
	vector<Rect> Rects;
	while (itc != contours.end())
	{
		Rect mr = boundingRect(Mat(*itc));
		Rects.push_back(mr);
		itc++;
	}
	vector<Rect>::iterator itr = Rects.begin();
	int i = 0;
	int j = 0;
	while (i < Rects.size()-1)
	{
		int area;
		area = Rects[i].width*Rects[i].height;
		if (Rects[i + 1].width*Rects[i + 1].height > area)
		{
			j = i + 1;
		}
		i++;
	}
	Mat outImage1(outImage, Rects[j]);
	outImage = outImage1.clone();
	return outImage;
}

Mat sobel(Mat&src_gray, int SOBEL_X_WEIGHT, int SOBEL_Y_WEIGHT)
{
	Mat grad(src_gray.rows, src_gray.cols, CV_8UC1);
	int ddepth = -1;
	Mat grad_x, grad_y;
	Mat abs_grad_x, abs_grad_y;
	double scale[2] = { 1,0 };
	double dalta[2] = { 0,0 };
	Sobel(src_gray, grad_x, ddepth, 1, 0, 3, 1.0, 0.0, BORDER_DEFAULT);
	convertScaleAbs(grad_x, abs_grad_x);
	Sobel(src_gray, grad_y, ddepth, 0, 1, 3, 1.0, 0.0, BORDER_DEFAULT);
	convertScaleAbs(grad_y, abs_grad_y);
	addWeighted(abs_grad_x, SOBEL_X_WEIGHT, abs_grad_y, SOBEL_Y_WEIGHT, 0, grad);
	return grad;
}

Mat getHSVImage(Mat&VLImage)
{
	Mat HSVImage = VLImage.clone();
	Mat HSVImage1 = VLImage.clone();
	Mat HSVImage2 = VLImage.clone();
	Mat HSVImage3 = VLImage.clone();
	Mat hsvSplit[3];
	cvtColor(VLImage, HSVImage, COLOR_BGR2HSV);
	split(HSVImage, hsvSplit);
	equalizeHist(hsvSplit[2], hsvSplit[2]);
	merge(hsvSplit, 3, HSVImage);
	inRange(HSVImage, Scalar(100, 185, 46), Scalar(124, 220, 255), HSVImage1);
	inRange(HSVImage, Scalar(110, 50, 70), Scalar(124, 120, 200), HSVImage2);
	inRange(HSVImage, Scalar(20, 200, 200), Scalar(30, 255, 255), HSVImage3);
	bitwise_or(HSVImage1, HSVImage2, HSVImage);
	bitwise_or(HSVImage3, HSVImage, HSVImage);
	Mat element = getStructuringElement(MORPH_RECT, Size(25, 7));
	morphologyEx(HSVImage, HSVImage, MORPH_CLOSE, element);
	return HSVImage;
}

Mat colorMatch(const Mat&src, const bool adaptive_minsv)
{
	const float max_sv = 255;
	const float minref_sv = 64;
	const float minabs_sv = 95;
	const int min_h = 100;
	const int max_h = 140;
	Mat src_hsv;
	cvtColor(src, src_hsv, CV_BGR2HSV);
	vector<Mat> hsvSplit;
	split(src_hsv, hsvSplit);
	equalizeHist(hsvSplit[2], hsvSplit[2]);
	merge(hsvSplit, src_hsv);
	float diff_h = float((max_h - min_h) / 2);
	int avg_h = min_h + diff_h;
	int channels = src_hsv.channels();
	int nRows = src_hsv.rows;
	int nCols = src_hsv.cols*channels;
	if (src_hsv.isContinuous())
	{
		nCols *= nRows;
		nRows = 1;
	}
	int i, j;
	uchar *p;
	float s_all = 0;
	float v_all = 0;
	float count = 0;
	for (i = 0; i < nRows; ++i)
	{
		p = src_hsv.ptr<uchar>(i);
		for (j = 0; j < nCols; j += 3)
		{
			int H = int(p[j]);
			int S = int(p[j + 1]);
			int V = int(p[j + 2]);
			s_all += S;
			v_all += V;
			count++;
			bool colorMatched = false;
			if (H > min_h&&H < max_h)
			{
				int Hdiff = 0;
				if (H > avg_h)
					Hdiff = H - avg_h;
				else
					Hdiff = avg_h - H;
				float Hdiff_p = float(Hdiff) / diff_h;
				float min_sv = 0;
				if (true == adaptive_minsv)
					min_sv = minabs_sv - minref_sv / 2 * (1 - Hdiff_p);
				else
					min_sv = minabs_sv;
				if ((S > min_sv && S < max_sv) && (V > min_sv && V < max_sv))
				{
					colorMatched = true;
				}

			}
			if (colorMatched == true) {
				p[j] = 0; p[j + 1] = 0; p[j + 2] = 255;
			}
			else {
				p[j] = 0; p[j + 1] = 0; p[j + 2] = 0;
			}
		}
	}
	Mat src_grey;
	vector<Mat> hsvSplit_done;
	split(src_hsv, hsvSplit_done);
	src_grey = hsvSplit_done[2];
	return src_grey;
}

Mat getCImage(Mat&VLImage, int SX, int SY)
{
	Mat GrayVLImage(VLImage.rows, VLImage.cols, CV_8UC1);
	Mat GaussVLImage = VLImage.clone();
	Mat SobelImage = GrayVLImage.clone();
	Mat TImage(VLImage.rows, VLImage.cols, CV_8UC1);
	Mat CImage(VLImage.rows, VLImage.cols, CV_8UC1);
	GaussianBlur(VLImage, GaussVLImage, Size(5, 5), 0, 0, BORDER_DEFAULT);
	cvtColor(GaussVLImage, GrayVLImage, CV_RGB2GRAY);
	SobelImage = sobel(GrayVLImage, 1, 1);
	threshold(SobelImage, TImage, 0, 255, CV_THRESH_OTSU + CV_THRESH_BINARY);
	Mat element = getStructuringElement(MORPH_RECT, Size(SX, SY));
	morphologyEx(TImage, CImage, MORPH_CLOSE, element);
	return CImage;
}

bool verifySizes(RotatedRect mr)
{
	int MIN = 1;
	int MAX = 200;
	float error = 0.9f;
	float aspect = 3.f;
	int min = 44 * 14 * MIN;
	int max = 44 * 14 * MAX;
	float rmin = aspect - aspect * error;
	float rmax = aspect + aspect * error;
	int area = mr.size.width*mr.size.height;
	float r = (float)mr.size.width / (float)mr.size.height;
	if (r < 1)
	{
		r = (float)mr.size.height / (float)mr.size.width;
	}
	if ((area<min || area>max) || (r<rmin || r>rmax))
	{
		return false;
	}
	else
	{
		return true;
	}
}

Mat showResultMat(Mat src, Size rect_size, Point2f center, int index)
{
	Mat img_crop;
	getRectSubPix(src, rect_size, center, img_crop);
	Mat resultResized;
	resultResized.create((int)36, (int)136, src.type());
	resize(img_crop, resultResized, resultResized.size(), 0, 0, INTER_CUBIC);
	return resultResized;
}

int getContours(Mat&RImage, Mat&VLImage, vector<Mat>&resultVec)
{
	vector< vector< Point> > contours;
	findContours(RImage, contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE);
	Mat result;
	vector< vector<Point> >::iterator itc = contours.begin();
	vector<RotatedRect> rects;
	int t = 0;
	while (itc != contours.end())
	{
		RotatedRect mr = minAreaRect(Mat(*itc));
		if (!verifySizes(mr))
		{
			itc = contours.erase(itc);
		}
		else
		{
			++itc;
			rects.push_back(mr);
		}
	}
	int k = 1;
	for (int i = 0; i < rects.size(); i++)
	{
		RotatedRect minRect = rects[i];
		if (verifySizes(minRect))
		{
			float r = (float)minRect.size.width / (float)minRect.size.height;
			float angle = minRect.angle;
			Size rect_size = minRect.size;
			if (r < 1)
			{
				angle = 90 + angle;
				swap(rect_size.width, rect_size.height);
			}
			if (angle - 60 < 0 && angle + 60 > 0)
			{
				Mat rotmat = getRotationMatrix2D(minRect.center, angle-1, 1);
				Mat img_rotated;
				warpAffine(VLImage, img_rotated, rotmat, VLImage.size(), CV_INTER_CUBIC);
				Mat resultMat;
				resultMat = showResultMat(img_rotated, rect_size, minRect.center, k++);
				resultVec.push_back(resultMat);
			}
		}
	}
	return 0;
}

bool clearLiuDing(Mat&Image)
{
	vector<float>fJump;
	int whiteCount = 0;
	const int x = 7;
	Mat jump = Mat::zeros(1, Image.rows, CV_32F);
	for (int i = 0; i < Image.rows; i++)
	{
		int jumpCount = 0;
		for (int j = 0; j < Image.cols - 1; j++)
		{
			if (Image.at<uchar>(i, j) != Image.at<uchar>(i, j + 1))
				jumpCount++;
			if (Image.at<uchar>(i, j) == 255)
				whiteCount++;
		}
		jump.at<float>(i) = (float)jumpCount;
	}
	int iCount = 0;
	for (int i = 0; i < Image.rows; i++)
	{
		fJump.push_back(jump.at<float>(i));
		if (jump.at<float>(i) >= 16 && jump.at<float>(i) <= 45)
		{
			iCount++;
		}
	}
	if (iCount + 1.0 / Image.rows <= 0.40)
	{
		return false;
	}
	if (whiteCount*1.0 / (Image.rows*Image.cols) < 0.15 || whiteCount * 1.0 / (Image.rows*Image.cols) > 0.50)
	{
		return false;
	}
	for (int i = 0; i < Image.rows; i++)
	{
		if (jump.at<float>(i) <= x)
		{
			for (int j = 0; j < Image.cols; j++)
			{
				Image.at<char>(i, j) = 0;
			}
		}
	}
	return true;

}

int getSpecificRect(const vector<Rect>&vecRect)
{
	vector<int>xpositions;
	int maxHeight = 0;
	int maxWidth = 0;
	for (size_t i = 0; i < vecRect.size(); i++)
	{
		xpositions.push_back(vecRect[i].x);
		if (vecRect[i].height > maxHeight)
		{
			maxHeight = vecRect[i].height;
		}
		if (vecRect[i].width > maxWidth)
		{
			maxWidth = vecRect[i].width;
		}
	}
	int specIndex = 0;
	for (size_t i = 0; i < vecRect.size(); i++)
	{
		Rect mr = vecRect[i];
		float midx = (mr.x + mr.width / 2) / 136.0;
		if ((mr.width > maxWidth * 0.6 || mr.height > maxHeight * 0.6) && (midx < float(2.0 / 7.0) && midx > float(1.0 / 7.0)))
		{
			specIndex = i;
		}
	}
	return specIndex;
}

bool verifyCharSizes(Mat r) {
	float aspect = 45.0f / 90.0f;
	float charAspect = (float)r.cols / (float)r.rows;
	float error = 0.7f;
	float minHeight = 25.f;
	float maxHeight = 35.f;
	float minWidth = 1.f;
	float minAspect = 0.05f;
	float maxAspect = aspect + aspect * error;
	int area = countNonZero(r);
	int bbArea = r.cols * r.rows;
	int percPixels = area / bbArea;

	if (percPixels <= 1 && charAspect > minAspect && charAspect < maxAspect && (r.rows >= minHeight || area > 30) && r.rows < maxHeight && area > 30 && r.cols > minWidth)
		return true;
	else
		return false;
}

void againVerify(vector<Mat>&Sign1)
{
	vector<Mat>::iterator it = Sign1.begin();
	vector<int> data;
	vector<int>::iterator itd = data.begin();
	int i = 0, min, j = 0;
	while (it != Sign1.end())
	{
		data.push_back(countNonZero(*it));
		it++;
	}
	min = data[0];
	while (i < data.size())
	{
		if (min > data[i])
		{
			min = data[i];
			j = i;
		}
		i++;
	}
	it = Sign1.begin() + j;
	it = Sign1.erase(it);
}

int getSign(vector<Mat>resultVec, vector< vector<Mat> >&Sign)
{
	vector< vector<Rect> > Rects;
	vector<Mat>VSign;
	vector<Mat>::iterator itr = resultVec.begin();
	vector<Mat>::iterator itv = VSign.begin();
	vector<int> sprcIndex;
	int i = 0;
	int k = 0;
	if (!(resultVec.empty()))
	{
		while (itr != resultVec.end())
		{
			vector< vector<Point> > contours;
			VSign.push_back(*itr);
			cvtColor(resultVec[i], VSign[i], CV_BGR2GRAY);
			threshold(VSign[i], VSign[i], 0, 255, CV_THRESH_OTSU + CV_THRESH_BINARY);
			float NZ = (float)countNonZero(VSign[i]) / (float)(VSign[i].rows*VSign[i].cols);
			if (NZ > 0.7 || NZ < 0.1)
			{
				itr = resultVec.erase(itr);
				continue;
			}
			else if (NZ > 0.4)
			{
				Mat element = getStructuringElement(MORPH_RECT, Size(3, 3));
				erode(VSign[i], VSign[i], element);
			}
			if (!(clearLiuDing(VSign[i])))
			{
				itr = resultVec.erase(itr);
				continue;
			}
			findContours(VSign[i], contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE);
			vector< vector<Point> >::iterator itc = contours.begin();
			vector<Rect> Rects1;
			vector<Mat> Sign1;
			while (itc != contours.end())
			{
				Rect mr = boundingRect(Mat(*itc));
				Rects1.push_back(mr);
				itc++;
			}
			sort(Rects1.begin(), Rects1.end(), cmpR);
			vector<Rect>::iterator itex = Rects1.begin();
			while (itex != Rects1.end())
			{
				Mat auxRoi(VSign[i], *itex);
				Sign1.push_back(auxRoi);
				itex++;
			}
			if (Rects1.size() < 7)
			{
				Rects1.clear();
				continue;
			}
			Rects.push_back(Rects1);
			sprcIndex.push_back(getSpecificRect(Rects1));
			int w = Sign1[sprcIndex[i]].cols;
			int x = Rects1[sprcIndex[i]].x - 1.2*w;
			if (x < 0)
			{
				Rects1[0] = Rect(1, Rects1[sprcIndex[i]].y, Rects1[sprcIndex[i]].width, Rects1[sprcIndex[i]].height);
			}
			else
			{
				Rects1[0] = Rects1[sprcIndex[i]] + Point(-(1.2*w), 0);
			}
			vector<Mat>::iterator its = Sign1.begin();
			int ie = 0;
			while (ie < sprcIndex[i])
			{
				its = Sign1.erase(its);
				ie++;
			}
			Mat auxRoi(VSign[i], Rects1[0]);
			Sign1.push_back(auxRoi);
			its = Sign1.begin();
			while (its != Sign1.end())
			{
				if (!(verifyCharSizes(*its)))
				{
					its = Sign1.erase(its);
					k++;
				}
				else
				{
					++its;
				}
			}
			if (Sign1.size() > 7)
				againVerify(Sign1);
			if (Sign1.size() != 7)
			{
				Sign1.clear();
				i++;
				itr++;
				continue;
			}
			Sign.push_back(Sign1);
			i++;
			itr++;
			contours.clear();
			Rects1.clear();
		}
		return 1;
	}
	else return 0;
}

Rect getCenterRect(Mat&in)
{
	Rect _rect;
	int top = 0;
	int bottom = in.rows - 1;
	for (int i = 0; i < in.rows; i++)
	{
		bool bFind = false;
		for (int j = 0; j < in.cols; j++)
		{
			if (in.data[i*in.step[0] + j] > 20)
			{
				top = i;
				bFind = true;
				break;
			}
		}
		if (bFind)
		{
			break;
		}
	}
	for (int i = in.rows - 1; i >= 0; i--)
	{
		bool bFind = false;
		for (int j = 0; j < in.cols; j++)
		{
			if (in.data[i*in.step[0] + j] > 20)
			{
				bottom = i;
				bFind = true;
				break;
			}
		}
		if (bFind)
		{
			break;
		}
	}
	int left = 0;
	int right = in.cols - 1;
	for (int j = 0; j < in.cols; j++)
	{
		bool bFind = false;
		for (int i = 0; i < in.rows; ++i)
		{
			if (in.data[i*in.step[0] + j] > 20)
			{
				left = j;
				bFind = true;
				break;
			}
		}
		if (bFind)
		{
			break;
		}
	}
	for (int j = in.cols - 1; j >= 0; j--)
	{
		bool bFind = false;
		for (int i = 0; i < in.rows; ++i)
		{
			if (in.data[i*in.step[0] + j] > 20)
			{
				right = j;
				bFind = true;
				break;
			}
		}
		if (bFind)
		{
			break;
		}
	}
	_rect.x = left;
	_rect.y = top;
	_rect.width = right - left + 1;
	_rect.height = bottom - top + 1;
	return _rect;
}

Mat cutTheRect(Mat&in, Rect&rect)
{
	int size = in.rows;
	Mat dstMat(size, size, CV_8UC1);
	dstMat.setTo(Scalar(0, 0, 0));
	int x = (int)floor((float)(size - rect.width) / 2.0f);
	int y = (int)floor((float)(size - rect.height) / 2.0f);
	for (int i = 0; i < rect.height; i++)
	{
		for (int j = 0; j < rect.width; j++)
		{
			dstMat.data[dstMat.step[0] * (i + y) + j + x] = in.data[in.step[0] * (i + rect.y) + j + rect.x];
		}
	}
	return dstMat;
}

float countOfBigValue(Mat&mat, int iValue)
{
	float iCount = 0.0;
	if (mat.rows > 1)
	{
		for (int i = 0; i < mat.rows; i++)
		{
			if (mat.data[i*mat.step[0]] > iValue)
			{
				iCount += 1.0;
			}
		}
		return iCount;
	}
	else
	{
		for (int i = 0; i < mat.cols; i++)
		{
			if (mat.data[i] > iValue)
			{
				iCount += 1.0;
			}
		}
		return iCount;
	}
}

Mat projectedHistogram(Mat img, int t)
{
	int sz = (t) ? img.rows : img.cols;
	Mat mhist = Mat::zeros(1, sz, CV_32F);
	for (int j = 0; j < sz; j++)
	{
		Mat data = (t) ? img.row(j) : img.col(j);
		mhist.at<float>(j) = countOfBigValue(data, 20);
	}
	double min, max;
	minMaxLoc(mhist, &min, &max);
	if (max > 0)
	{
		mhist.convertTo(mhist, -1, 1.0f / max, 0);
	}
	return mhist;
}

Mat charFeatures(Mat in, int sizeData, bool flage)
{
	const int VERTICAL = 0;
	const int HORIZONTAL = 1;
	Rect _rect = getCenterRect(in);
	Mat lowData;
	if (1)
	{
		Mat tempIn = cutTheRect(in, _rect);
		resize(tempIn, lowData, Size(sizeData, sizeData));
	}
	else
	{
		resize(in, lowData, Size(sizeData, sizeData));
	}

	Mat x = lowData;

	Mat vhist = projectedHistogram(lowData, VERTICAL);
	Mat hhist = projectedHistogram(lowData, HORIZONTAL);
	int numCols = vhist.cols + hhist.cols + lowData.cols*lowData.cols;
	Mat out = Mat::zeros(1, numCols, CV_32FC1);
	int j = 0;
	for (int i = 0; i < vhist.cols; i++)
	{
		out.at<float>(j) = vhist.at<float>(i);
		j++;
	}
	for (int i = 0; i < hhist.cols; i++)
	{
		out.at<float>(j) = hhist.at<float>(i);
		j++;
	}
	for (int x = 0; x < lowData.cols; x++)
	{
		for (int y = 0; y < lowData.rows; y++)
		{
			out.at<float>(j) += (float)lowData.at<unsigned char>(x, y);
			j++;
		}
	}
	return out;
}

void readImage(vector<int>&index, vector<Mat>&Image, int rize, int&Nfile)
{
	vector<String> filenames;
	
	glob(folder, filenames, true);
	int i = 0;
	int k = 7;
	String Value;
	int FeaturesNumber = rize * 2 + rize * rize;
	Nfile = filenames.size();
	while (i < filenames.size())
	{
		for (k = 7; k < filenames[i].size(); k++)
		{
			if (int(filenames[i][k]) == 92)
			{
				break;
			}
		}
		Value = filenames[i].substr(7, k - 7);
		for (int j = 0; j<kCharactersNumber; ++j)
		{
			if (kChars[j] == Value)
			{
				index.push_back(j);
			}
		}
		Mat image = imread(filenames[i]);
		cvtColor(image, image, CV_RGB2GRAY);
		threshold(image, image, 0, 255, CV_THRESH_OTSU + CV_THRESH_BINARY);
		Image.push_back(image);
		i++;
	}
}

void annTrain()
{
	Mat layers(1, 3, CV_32SC1);
	vector<int> index;
	vector<Mat> Image;
	clock_t STTime, ETTime;
	int Nfile;
	int rize = 20;
	layers.at<int>(0) = kAnnInput;
	layers.at<int>(1) = numNeurons;
	layers.at<int>(2) = kCharactersNumber;
	ann_ = ml::ANN_MLP::create();
	ann_->setLayerSizes(layers);
	ann_->setActivationFunction(cv::ml::ANN_MLP::SIGMOID_SYM, 1, 1);
	ann_->setTrainMethod(cv::ml::ANN_MLP::TrainingMethods::BACKPROP);
	ann_->setTermCriteria(cvTermCriteria(CV_TERMCRIT_ITER, 100, 0.01));
	ann_->setBackpropWeightScale(0.1);
	ann_->setBackpropMomentumScale(0.1);
	cout << "------------------载入训练样本------------------" << endl;
	readImage(index, Image, rize, Nfile);
	cout << "------------------样本载入完毕------------------" << endl;
	cout << "样本数量:" << Nfile <<endl;
	int n = 0;
	Mat trainData(Nfile, 440, CV_32F);
	Mat labelsMat = Mat::zeros(Nfile, kCharactersNumber, CV_32F);
	while (n < Image.size())
	{
		Mat feat = charFeatures(Image[n], rize, false);
		for (int m = 0; m < 440; m++)
		{
			trainData.at<float>(n, m) = feat.at<float>(0, m);
		}
		labelsMat.at<float>(n, index[n]) = 1.f;
		n++;
	}
	STTime = clock();
	cout << "------------------开始训练样本------------------" << endl;
	Ptr<TrainData> tData = TrainData::create(trainData, ROW_SAMPLE, labelsMat);
	ann_->train(tData);
	cout << "------------------样本训练完毕------------------" << endl;
	ETTime = clock();
	cout << "训练时间：" << ETTime - STTime << "ms" << endl;
	ann_->save(annXML);
}

int Classify(Mat f, float&maxVal, bool cTrain)
{
	bool flage = 0;
	int result = -1;
	Mat output(1, kCharactersNumber, CV_32FC1);
	Mat Iinput = charFeatures(f, 20, true);
	if (!cTrain)
	{
		cTrain = 0;
		flage = 1;
		lann_ = ANN_MLP::load(annXML);
	}
	if(flage)
		lann_->predict(Iinput, output);
	else
	    ann_->predict(Iinput, output);
	maxVal = -2.f;
	result = 0;
	for (int j = 0; j < kCharactersNumber; j++)
	{
		float val = output.at<float>(j);
		if (val > maxVal)
		{
			maxVal = val;
			result = j;
		}
	}
	return result;
}

int main(void)
{
	bool Train = 1;
	cout << "------------------正在读入图片------------------" << endl;
	Mat VLImage = imread(loadImage);
	cout << "------------------图片读取完毕------------------" << endl;
	cout << "------------------是否开启去雾------------------" << endl;
	int ele;
	bool flage = 0;
	while (1)
	{
		cout << "1、开启" << endl;
		cout << "2、关闭" << endl;
		cin >> ele;
		if (ele == 1)
		{
			flage = 1;
			break;
		}
		else if (ele == 2)
		{
			flage = 0;
			break;
		}
		else 
			cout << "输入错误，请重新输入。" << endl;
	}
	cout << "------------------是否训练样本------------------" << endl;
	while (1)
	{
		cout << "1、训练样本并保存数据" << endl;
		cout << "2、加载训练好的数据" << endl;
		cin >> ele;
		if (ele == 1)
		{
			Train = 1;
			break;
		}
		else if (ele == 2)
		{
			Train = 0;
			break;
		}
		else
			cout << "输入错误，请重新输入。" << endl;
	}
	if (flage)
	{
		cout << "------------------图片正在去雾------------------" << endl;
		VLImage = cleanFog(VLImage);
		cout << "------------------图片去雾完毕------------------" << endl;
	}
	cout << "------------------裁剪车牌轮廓------------------" << endl;
	Mat HSVImage(VLImage.rows, VLImage.cols, CV_8UC1);
	Mat CImage(VLImage.rows, VLImage.cols, CV_8UC1);
	Mat RImage(VLImage.rows, VLImage.cols, CV_8UC1);
	vector< vector<Mat> > Sign;
	int SX = 17;
	int SY = 3;
	if (VLImage.cols > 2000 || VLImage.rows > 1500)
	{
		SX = VLImage.cols / 100;
		SY = VLImage.rows / 100;
	}
	Mat HSVImage1 = VLImage.clone();
	cvtColor(VLImage, HSVImage1, CV_BGR2HSV);
	HSVImage = colorMatch(VLImage, true);
	CImage = getCImage(VLImage, SX, SY);
	bitwise_and(HSVImage, CImage, RImage);
	Mat element = getStructuringElement(MORPH_RECT, Size(SX, SY));
	morphologyEx(RImage, RImage, MORPH_CLOSE, element);
	vector<Mat>resultVec;
	getContours(RImage, VLImage, resultVec);
	cout << "------------------得到预计车牌------------------" << endl;
	cout << "------------------求取车牌字符------------------" << endl;
	getSign(resultVec, Sign);
	cout << "------------------得到车牌字符------------------" << endl;
	cout << "有效的车牌数量:" <<Sign.size()<< endl;
	vector< vector<int> > plant;
	vector< vector<float> > maxVal;
	vector< vector<Mat> >::iterator its = Sign.begin();
	int i = 0;

	if (Train)
	{
		annTrain();
	}

	cout << "------------------开始识别车牌------------------" << endl;
	while (its != Sign.end())
	{
		vector<int> plant1;
		vector<float> maxVal1;
		int j = 0;
		vector<Mat>::iterator its1 = Sign[i].begin();
		while (its1 != Sign[i].end())
		{
			int num;
			float Val;
			num = Classify(Sign[i][j], Val, Train);
			plant1.push_back(num);
			maxVal1.push_back(Val);
			j++;
			its1++;
		}
		plant.push_back(plant1);
		maxVal.push_back(maxVal1);
		i++;
		its++;
	}

	map<int, string> m_map;

	m_map.insert(pair<int, string>(1, "川"));
	m_map.insert(pair<int, string>(2, "鄂"));
	m_map.insert(pair<int, string>(3, "赣"));
	m_map.insert(pair<int, string>(4, "甘"));
	m_map.insert(pair<int, string>(5, "贵"));
	m_map.insert(pair<int, string>(6, "桂"));
	m_map.insert(pair<int, string>(7, "黑"));
	m_map.insert(pair<int, string>(8, "沪"));
	m_map.insert(pair<int, string>(9, "冀"));
	m_map.insert(pair<int, string>(10, "津"));
	m_map.insert(pair<int, string>(11, "京"));
	m_map.insert(pair<int, string>(12, "吉"));
	m_map.insert(pair<int, string>(13, "辽"));
	m_map.insert(pair<int, string>(14, "鲁"));
	m_map.insert(pair<int, string>(15, "蒙"));
	m_map.insert(pair<int, string>(16, "闽"));
	m_map.insert(pair<int, string>(17, "宁"));
	m_map.insert(pair<int, string>(18, "青"));
	m_map.insert(pair<int, string>(19, "琼"));
	m_map.insert(pair<int, string>(20, "陕"));
	m_map.insert(pair<int, string>(21, "苏"));
	m_map.insert(pair<int, string>(22, "晋"));
	m_map.insert(pair<int, string>(23, "皖"));
	m_map.insert(pair<int, string>(24, "湘"));
	m_map.insert(pair<int, string>(25, "新"));
	m_map.insert(pair<int, string>(26, "豫"));
	m_map.insert(pair<int, string>(27, "渝"));
	m_map.insert(pair<int, string>(28, "粤"));
	m_map.insert(pair<int, string>(29, "云"));
	m_map.insert(pair<int, string>(30, "藏"));
	m_map.insert(pair<int, string>(31, "浙"));

	vector< vector<char> > PLANT;
	i = 0;
	vector< vector<int> >::iterator itp = plant.begin();
	while (itp != plant.end())
	{
		vector<char> PLANT1;
		int j = 0;
		vector<int>::iterator itp1 = plant[i].begin();
		while (itp1 != plant[i].end())
		{
			if (plant[i][j] < 34)
			{
				PLANT1.push_back(*kChars[plant[i][j]]);
			}
			j++;
			itp1++;
		}
		PLANT.push_back(PLANT1);
		i++;
		itp++;
	}
	i = 0;
	while (i < PLANT.size())
	{
		char display[7];
		int j = 0;
		while (j < PLANT[i].size())
		{
			display[j] = PLANT[i][j];
			j++;
		}
		int n = 0;
		float PRVal=0;
		while (n < maxVal[i].size())
		{
			PRVal = PRVal+maxVal[i][n];
			n++;
		}
		PRVal = (PRVal / 7.f) * 100.f;
		display[6] = '\0';
		cout << "第"<<i+1<<"张车牌号为:"<<m_map[plant[i][6] - 33].c_str() << display <<"\n"<< "正确率为:" << PRVal <<"%"<<endl;
		i++;
	}
	imshow("原图",VLImage);
	waitKey(0);
	return 0;
}
