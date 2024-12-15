#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <set>
#include <array>
#include <unordered_map>


// static std::unordered_map<int, int8_t> efmp = {{1, 2}, {2, 3}};
// 用于存储输入数据和输出数据的结构体
struct Data
{
	std::vector<float> inputs;
	int output;
};

// 从CSV文件中读取数据
std::vector<Data> read_csv(const std::string &filename)
{
	std::vector<Data> dataset;
	std::ifstream file(filename);
	std::string line;

	if (!file.is_open())
	{
		std::cerr << "Unable to open file" << std::endl;
		return dataset;
	}

	while (std::getline(file, line))
	{
		std::istringstream sstream(line);
		std::string value;
		std::vector<float> inputs;
		int output;

		for (int i = 0; i < 128; ++i)
		{
			std::getline(sstream, value, ',');
			inputs.push_back(std::stof(value));
		}
		std::getline(sstream, value, '\n');
		output = std::stoi(value);

		Data data = {inputs, output};
		dataset.push_back(data);
	}

	file.close();
	return dataset;
}

std::string mapToString(const std::unordered_map<int, int8_t> mps)
{
	std::ostringstream oss;
	oss << "{";
	for (auto it : mps)
	{
		oss << "{";
		oss << static_cast<int>(it.first) << "," << static_cast<int>(it.second);
		oss << "} ";
		oss << ", ";
	}
	oss << "}";
	return oss.str();
}

std::string vectorToString(std::vector<int8_t> &vec)
{
	std::ostringstream oss;
	oss << "{";
	for (size_t i = 0; i < vec.size(); ++i)
	{
		oss << static_cast<int>(vec[i]);
		if (i < vec.size() - 1)
		{
			oss << ", ";
		}
	}
	oss << "}";
	return oss.str();
}

int hash_float_array(float *array)
{
	int seed = 0;
	for (int i = 0; i < 128; i++)
	{
		int int_value = ((int)array[i]) * ((int)array[i]) * i * (i - 1);
		seed += int_value;
	}
	return seed;
}

int main()
{
	std::string filename = "./query_results.csv"; // CSV文件名
	std::vector<Data> dataset = read_csv(filename);
	std::vector<int8_t> data;
	std::set<int> hashs;
	std::unordered_map<int, int8_t> efmp;
	for (size_t i = 0; i < dataset.size(); ++i)
	{
		int hint = hash_float_array(dataset[i].inputs.data());
		data.push_back((int8_t)std::min(dataset[i].output,122));
		if (!hashs.count(hint))
		{
			hashs.insert(hint);
		}
		else
		{
			// std::cout << i << " " << hint << " error" << std::endl;
		}
		efmp[hint] = (int8_t)std::min(dataset[i].output, 122);
	}
	// std::string vecStr = vectorToString(data);
    // std::cout << "static std::array<int8_t,10000> efs =" << vecStr << ";" << std::endl;
	std::string vecStr = mapToString(efmp);
	std::cout << "static std::unordered_map<int, int8_t> efs = " << vecStr << ";" << std::endl;
	return 0;
}

// #include <iostream>
// #include <fstream>
// #include <sstream>
// #include <vector>
// #include <string>
// #include <cmath>
// #include <random>

// // 用于存储输入数据和输出数据的结构体
// struct Data
// {
// 	std::vector<float> inputs;
// 	int output;
// };

// // 从CSV文件中读取数据
// std::vector<Data> read_csv(const std::string &filename)
// {
// 	std::vector<Data> dataset;
// 	std::ifstream file(filename);
// 	std::string line;

// 	if (!file.is_open())
// 	{
// 		std::cerr << "Unable to open file" << std::endl;
// 		return dataset;
// 	}

// 	while (std::getline(file, line))
// 	{
// 		std::istringstream sstream(line);
// 		std::string value;
// 		std::vector<float> inputs;
// 		int output;

// 		for (int i = 0; i < 128; ++i)
// 		{
// 			std::getline(sstream, value, ',');
// 			inputs.push_back(std::stof(value));
// 		}
// 		std::getline(sstream, value, '\n');
// 		output = std::stoi(value);

// 		Data data = {inputs, output};
// 		dataset.push_back(data);
// 	}

// 	file.close();
// 	return dataset;
// }

// std::string vectorToString(const std::vector<int8_t> &vec)
// {
// 	std::ostringstream oss;
// 	oss << "{";
// 	for (size_t i = 0; i < vec.size(); ++i)
// 	{
// 		oss << static_cast<int>(vec[i]);
// 		if (i < vec.size() - 1)
// 		{
// 			oss << ", ";
// 		}
// 	}
// 	oss << "}";
// 	return oss.str();
// }

// int hash_float_array(float *array)
// {
// 	int seed = 0;
// 	for (int i = 0; i < 128; i++)
// 	{
// 		int int_value = (int)array[i];
// 		seed ^= int_value;
// 	}
// 	return seed % (int)0x7fffffff;
// }

// class LinearCongruentialGenerator
// {
// public:
// 	LinearCongruentialGenerator()
// 	{
// 		this->seed_ = 1035;
// 	}

// 	LinearCongruentialGenerator(unsigned long seed)
// 	{
// 		this->seed_ = seed;
// 	}

// 	double
// 	next()
// 	{
// 		seed_ = (a_ * seed_ + c_) % m_;
// 		return (double)seed_ / m_;
// 	}

// 	void
// 	seed(unsigned long seed)
// 	{
// 		seed_ = seed;
// 	}

// private:
// 	static constexpr unsigned long a_ = 1664525;	// 乘数
// 	static constexpr unsigned long c_ = 1013904223; // 增量
// 	static constexpr unsigned long m_ = 4294967296; // 模数
// 	unsigned long seed_;
// };

// class UniformRealDistribution
// {
// public:
// 	UniformRealDistribution(double a, double b) : min_(a), max_(b)
// 	{
// 	}

// 	// double
// 	// operator()(LinearCongruentialGenerator &generator)
// 	// {
// 	// 	double u1 = generator.next();
// 	// 	double u2 = generator.next();
// 	// 	double mx1 = (u1 > u2) ? u1 : u2;
// 	// 	double mx2 = (u1 > u2) ? u2 : u1;
// 	// 	std::cout << "rate " << mx2 / mx1 << std::endl;
// 	// 	return min_ + mx2 * (max_ - min_) / mx1;
// 	// }

// 	double
//     operator()(LinearCongruentialGenerator& generator) {
//         double u = generator.next();
//         return min_ + u * (max_ - min_);
//     }

// private:
// 	double min_;
// 	double max_;
// };

// int main() {
//     std::string filename = "./query_results.csv"; // CSV文件名
//     std::vector<Data> dataset = read_csv(filename);
//     std::vector<int8_t> data;
//     std::set<int> hashs;
//     for (size_t i = 0; i < dataset.size(); ++i) {
//         int hint = hash_float_array(dataset[i].inputs.data());
//         // data.push_back((int8_t)std::min(dataset[i].output,122));
//         if(!hashs.contain(hint)) {
//             hashs.insert(hint);
//         }else {
//             return 1;
//         }
//     }
//     // std::string vecStr = vectorToString(data);
//     // std::cout << "constexpr std::vector<int8_t> efs = " << vecStr << ";" << std::endl;
//     return 0;
// }

// int main()
// {
	
// 	LinearCongruentialGenerator lg;
// 	lg.seed((unsigned long)(10 * 1035));
// 	std::default_random_engine level_generator_;
// 	// for (int i = 0; i < 100; i++)
// 	// {
// 	// 	// UniformRealDistribution distribution(0.0, 1.0);
// 	// 	// double r = -log(distribution(lg)) * 15;
// 	// 	// std::cout << r << std::endl;

// 	// 	std::uniform_real_distribution<double> distribution(0.0, 1.0);
//     //     double r = -log(distribution(level_generator_)) * 15;
//     //     std::cout << r << std::endl;
// 	// }
// 	double mult_ = 1 / log(1.0 * 16);
// 	std::cout << mult_ << std::endl;
// 	return 0;
// }